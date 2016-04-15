/*
 * Copyright (c) 2013
 *	The President and Fellows of Harvard College.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE UNIVERSITY AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE UNIVERSITY OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <types.h>
#include <current.h>
#include <lib.h>
#include <clock.h>
#include <spinlock.h>
#include <synch.h>
#include <vm.h>
#include <proc.h>
#include <addrspace.h>
#include <kern/errno.h>
#include <bitmap.h>
#include <swap.h>
#include <coremap.h>

struct coremap *coremap;


/*
 * coremap_entry_init
 *
 * Initializes the coremap entry
 */ 
void
coremap_entry_init(struct coremap_entry *coremap_entry) {

	/* Check that the coremap_entry is not NULL */
	KASSERT(coremap_entry != NULL);

	/* Set the fields in the coremap_entry to their default values */
	coremap_entry->ce_addrspace = NULL;
	coremap_entry->ce_allocated = false;
	coremap_entry->ce_foruser = true;
	coremap_entry->ce_busy = false;
	coremap_entry->ce_next = 0;
	coremap_entry->ce_pgentry = NULL;
	coremap_entry->ce_swapoffset = -1;
}

/*
 * coremap_bootstrap
 *
 * Sets up the coremap during bootup
 */
void
coremap_bootstrap(void) {
	paddr_t firstpaddr;
	paddr_t lastpaddr;
	unsigned long total_npages;

	/* Determine the size of the ram */
	lastpaddr = ram_getsize();
	/* Check that lastpaddr is aligned to 4K */
	KASSERT((lastpaddr & PAGE_FRAME) == lastpaddr);
	/* Calculate the total number of physical pages in the system based on
	 * lastpaddr */
	total_npages = (unsigned long)(lastpaddr / PAGE_SIZE);

	/* Allocate the coremap */
	coremap = kmalloc(sizeof(struct coremap));
	if (coremap == NULL) {
		panic("kmalloc failed in coremap_boostrap\n");
	}

	/* Initialise the coremap spinlock */
	spinlock_init(&coremap->c_spinlock);

	/* Allocate the array of coremap entries.  The number of coremap entries
	 * equals the number of physical pages in the system. */
	coremap->c_entries = (struct coremap_entry*)kmalloc(total_npages * sizeof(struct
	coremap_entry));
	if (coremap->c_entries == NULL) {
		panic("kmalloc failed in coremap_bootstrap\n");
	}

	/* Initialize all of the coremap entries */
	for (unsigned long i=0; i<total_npages; i++) {
		coremap_entry_init(&coremap->c_entries[i]);
	}

	/* At this point, some of the memory is already being used by the kernel
	 * for data structures such as the coremap.  The address firstpaddr is
	 * the first physical address which is not being used.  We mark all
	 * addresses before firstpaddr as being allocated and in use by the
	 * kernel. */
	firstpaddr = ram_getfirstfree();

	KASSERT((firstpaddr & PAGE_FRAME) == firstpaddr);

	for (unsigned long i=0; i<(unsigned long)(firstpaddr/PAGE_SIZE); i++) {
		coremap->c_entries[i].ce_allocated = true;
		coremap->c_entries[i].ce_foruser = false;
	}

	/* Set aside a number of physical pages which can only be used by the
	 * kernel. */
	for (unsigned long i=(unsigned long)(firstpaddr/PAGE_SIZE); i<(unsigned
	long)(firstpaddr/PAGE_SIZE + N_RESERVEDKPAGES ); i++) {
		coremap->c_entries[i].ce_foruser = false;
	}

	/* Set coremap->c_npages to be the total number of physical pages in the
	 * system. */
	coremap->c_npages = total_npages;

	/* Now that we have finished setting up the coremap, mark
	 * coremap->c_ready as true. */
	coremap->c_ready = true;

	/* The address firstpaddr is the first physical address which can be
	 * allocated and freed by the kernel. */
	coremap->c_kernelpbase = (int)(firstpaddr/PAGE_SIZE);

	/* The address after the block of reserved kernel physical pages is the
	 * first address which can be used by the user process. */
	coremap->c_userpbase = (int)(firstpaddr/PAGE_SIZE + N_RESERVEDKPAGES);
}

/*
 * coremap_ready
 *
 * Returns whether the coremap is ready to be used
 */
bool
coremap_ready(void) {
	if (coremap == NULL) {
		return false;
	} else {
		return coremap->c_ready;
	}
}

/*
 * coremap_getkpages
 *
 * Used by kmalloc to get physical pages for the kernel
 */
paddr_t
coremap_getkpages(unsigned long npages) {	

	paddr_t paddr;
	unsigned long total_npages;
	unsigned long start;
	unsigned long end;

	total_npages = coremap->c_npages;

	while (1) {
		
		start = coremap->c_kernelpbase;
		spinlock_acquire(&coremap->c_spinlock);
			
		while (start < total_npages) {

			/* Go through the entire coremap starting from
			 * coremap->c_kernelpbase */

			if (!coremap->c_entries[start].ce_allocated) {

				/* If we encounter a free page, we see if the
				 * pages contiguous to it are also free */

				end = start+npages-1;
				if (end >= total_npages) {

					/* Break if we have reached near the end
					 * of the coremap and have not found the
					 * required number of contiguous free
					 * pages. */ 

					break;
				}

				while (end > start) {

					/* Start from what would be the end of
					 * the contiguous block of kernal pages
					 * and work our way back towards the
					 * start of the block, checking to see
					 * if each page is free */

					if (!coremap->c_entries[end].ce_allocated) {

						/* If we encounter a free page,
						 * check that its field are set
						 * to the appropriate values */

						KASSERT(coremap->c_entries[end].ce_addrspace
						== NULL);
						KASSERT(!coremap->c_entries[end].ce_busy);
						KASSERT(coremap->c_entries[end].ce_next
						== 0);
						KASSERT(coremap->c_entries[end].ce_pgentry == NULL);
						KASSERT(coremap->c_entries[end].ce_swapoffset == -1);
					}

					if (coremap->c_entries[end].ce_allocated) {

						/* Break if we encounter a page
						 * which is already allocated */

						break;

					} else {

						/* Decrement the end variable if
						 * we encounter a free page */

						end--;
					}
				}
			
				if (start == end) {

					/* If the variable start equals the
					 * variable end, then we have found a
					 * contiguous block of free pages for
					 * the kernel*/

					for (unsigned i=start; i<start+npages; i++) {

						/* Mark each page as being
						 * allocated for the kernel */

						coremap->c_entries[i].ce_allocated = true;
						coremap->c_entries[i].ce_foruser =
						false;

						if (i < start+npages-1) {

							/* Mark ce_next of each
							 * coremap entry as
							 * being 1 if the next
							 * adjacent page forms a
							 * part of the
							 * contiguous block of
							 * kernel pages */

							coremap->c_entries[i].ce_next = 1;

						} else {

							/* The coremap entry
							 * representing the last page in the
							 * contiguous block of
							 * kernel pages has its
							 * ce_next marked as 0.
							 */

							coremap->c_entries[i].ce_next = 0;
						}		
					}

	
					/* Release the coremap spinlock and
					 * return the physical address of the
					 * first page in the block of kernel
					 * pages */

					paddr = (paddr_t)(start*PAGE_SIZE);
					spinlock_release(&coremap->c_spinlock);
					return paddr;
				} 
						
			}

			/* If we have reached this point, we hve not yet found a
			 * contiguous block of free kernel pages.  We increment
			 * start and continue searching the coremap. */

			start++;
		}

		/* If we have reached this point, we cannot find a contigous
		 * block of free kernel pages.  We have no choice but to wait
		 * for the page daemon to create free pages for us. */

		spinlock_release(&coremap->c_spinlock);

		if (kswap->sw_diskfull) {

			/* If the disk is full,there is no chance that the page
			 * daemon can create free pages so we return 0. */

			return 0;
		}
		
		/* Busy wait until the page daemon produces free pages */

		while(!kswap->sw_pgevicted);

	}

}

/*
 * coremap_getpage
 *
 * Gets pages for user processes.
 */
int
coremap_getpage(int *pg_entry, struct addrspace *as) {
	
	/* Pages used by processes are referenced by page tables.  The function
	 * coremap_getpage takes as its input a pointer to a page table entry
	 * and the address space which holds the page table entry. */


	/* Check that the page table entry pointer and the address pointer are
	 * not NULL */	
	KASSERT(pg_entry != NULL);
	KASSERT(as != NULL);

	paddr_t paddr;
	paddr_t pgvictim;
	int c_index;
	int result;
	unsigned long total_npages;
	unsigned long start;

	total_npages = coremap->c_npages;

	spinlock_acquire(&coremap->c_spinlock);
	start = coremap->c_userpbase;

	for (unsigned long i=start; i<total_npages; i++) {

		/* In the section of physical memory not reserved exclusively
		 * for the kernel, find a free page */

		if (!coremap->c_entries[i].ce_allocated ) {

			/* Check that the coremap entry corresponding to the
			 * free physical page has the correct values */

			KASSERT(coremap->c_entries[i].ce_foruser);
			KASSERT(!coremap->c_entries[i].ce_busy);
			KASSERT(coremap->c_entries[i].ce_next == 0);
			KASSERT(coremap->c_entries[i].ce_pgentry == NULL);
			KASSERT(coremap->c_entries[i].ce_swapoffset ==
			-1);

		
			/* Mark the coremap entry as being allocated to a user
			 * process.  The ce_addrspace and ce_foruser fields
			 * should point to the address space and page table
			 * entry respectively. */	

			coremap->c_entries[i].ce_addrspace = as;
			coremap->c_entries[i].ce_allocated = true;
			coremap->c_entries[i].ce_foruser = true;
			coremap->c_entries[i].ce_next = 0;

			/* Calculate what the physical page address is based on
			 * its index in the coremap entry */

			paddr = (paddr_t)((i*PAGE_SIZE) >> 12);

			/* Place the physical page address in the page table
			 * entry and release the coremap spinlock */
			*pg_entry &= ~PG_FRAME;
			*pg_entry |= paddr;
			coremap->c_entries[i].ce_pgentry = pg_entry;
			spinlock_release(&coremap->c_spinlock);
			return 0;

		}
			
	}

	/* Release the coremap spinlock and yield to another thread */
	spinlock_release(&coremap->c_spinlock);
	thread_yield();

	/* Since there are no free physical pages, we need to evict a page.
	 * First, find a page to evict. */
	sw_getpage(&pgvictim);

	/* Check that the coremap entry corresponding to the page we selected for
	 * eviction has the correct values. */
	KASSERT(!spinlock_do_i_hold(&coremap->c_spinlock));
	c_index = (int)(pgvictim/PAGE_SIZE);
	KASSERT(coremap->c_entries[c_index].ce_allocated);
	KASSERT(coremap->c_entries[c_index].ce_foruser);
	KASSERT(coremap->c_entries[c_index].ce_busy);
	KASSERT((paddr_t)((*coremap->c_entries[c_index].ce_pgentry & PG_FRAME)
	<< 12) == pgvictim);
		
	/* Evict the page */
	result = sw_evictpage(pgvictim);
	if (result) {
		spinlock_acquire(&coremap->c_spinlock);
		coremap->c_entries[c_index].ce_busy = false;
		spinlock_release(&coremap->c_spinlock);
		return result;
	}

	KASSERT(coremap->c_entries[c_index].ce_busy);

	/* If we have successfully evicted a page, we place its address in the
	 * page table entry */
	spinlock_acquire(&coremap->c_spinlock);	
	paddr = pgvictim >> 12;
	*pg_entry &= ~PG_FRAME;
	*pg_entry |= paddr; 

	/* Mark the coremap entry as being allocated for the user */
	coremap->c_entries[c_index].ce_allocated = true;
	coremap->c_entries[c_index].ce_addrspace = as;
	coremap->c_entries[c_index].ce_foruser = true;
	coremap->c_entries[c_index].ce_next = 0;
	coremap->c_entries[c_index].ce_pgentry = pg_entry;
	coremap->c_entries[c_index].ce_swapoffset = -1;
	coremap->c_entries[c_index].ce_busy = false;

	/* Release the coremap spinlock and return 0 */
	spinlock_release(&coremap->c_spinlock);
	return 0;
}

/*
 * coremap_freekpages
 *
 * Frees kernel pages
 */
void
coremap_freekpages(paddr_t paddr) {
	int c_index;

	spinlock_acquire(&coremap->c_spinlock);
	c_index = (int)(paddr / PAGE_SIZE);
	
	while (coremap->c_entries[c_index].ce_next != 0) {
		
		/* Check that each coremap entry corresponding to the kernel
		 * page has the appropriate values */

		KASSERT(coremap->c_entries[c_index].ce_allocated);
		KASSERT(!coremap->c_entries[c_index].ce_foruser);
		KASSERT(!coremap->c_entries[c_index].ce_busy);
		KASSERT(coremap->c_entries[c_index].ce_pgentry == NULL);
		KASSERT(coremap->c_entries[c_index].ce_swapoffset == -1);

		/* If the kernel page falls within the section of physical
		 * memory which is not exclusively reserved for the kernel, mark
		 * the page as being available for user processes */
		if (c_index >= coremap->c_userpbase) {
			coremap->c_entries[c_index].ce_foruser = true;

		} else {
			coremap->c_entries[c_index].ce_foruser = false;
		}
			
		/* Mark the coremap entry to indicate that the kernel page is
		 * free */	
		coremap->c_entries[c_index].ce_allocated = false;
		coremap->c_entries[c_index].ce_next = 0;
		c_index++;
	}

	/* Mark the last page in the contiguous block of kernel pages as being
	 * free */

	KASSERT(coremap->c_entries[c_index].ce_allocated);
	KASSERT(!coremap->c_entries[c_index].ce_foruser);
	KASSERT(!coremap->c_entries[c_index].ce_busy);
	KASSERT(coremap->c_entries[c_index].ce_pgentry == NULL);
	KASSERT(coremap->c_entries[c_index].ce_swapoffset == -1);

	if (c_index >= coremap->c_userpbase) {
		coremap->c_entries[c_index].ce_foruser = true;

	} else {
		coremap->c_entries[c_index].ce_foruser = false;
	}

	coremap->c_entries[c_index].ce_allocated = false;

	/* Release the coremap spinlock */
	spinlock_release(&coremap->c_spinlock);
}
