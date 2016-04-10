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

void
coremap_entry_init(struct coremap_entry *coremap_entry) {
	KASSERT(coremap_entry != NULL);

	coremap_entry->ce_addrspace = NULL;
	coremap_entry->ce_allocated = false;
	coremap_entry->ce_foruser = false;
	coremap_entry->ce_busy = false;
	coremap_entry->ce_next = 0;
	coremap_entry->ce_pgentry = NULL;
	coremap_entry->ce_swapoffset = -1;
}

void
coremap_bootstrap(void) {
	paddr_t firstpaddr;
	paddr_t lastpaddr;
	unsigned long total_npages;

	lastpaddr = ram_getsize();
	KASSERT((lastpaddr & PAGE_FRAME) == lastpaddr);
	total_npages = (unsigned long)(lastpaddr / PAGE_SIZE);

	coremap = kmalloc(sizeof(struct coremap));
	if (coremap == NULL) {
		panic("kmalloc failed in coremap_boostrap\n");
	}

	coremap->c_ready = false;

	spinlock_init(&coremap->c_spinlock);

	coremap->c_entries = (struct coremap_entry*)kmalloc(total_npages * sizeof(struct
	coremap_entry));
	if (coremap->c_entries == NULL) {
		panic("kmalloc failed in coremap_bootstrap\n");
	}

	for (unsigned long i=0; i<total_npages; i++) {
		coremap_entry_init(&coremap->c_entries[i]);
	}

	firstpaddr = ram_getfirstfree();
	KASSERT((firstpaddr & PAGE_FRAME) == firstpaddr);

	for (unsigned long i=0; i<(unsigned long)(firstpaddr/PAGE_SIZE); i++) {
		coremap->c_entries[i].ce_allocated = true;
	}

	coremap->c_npages = total_npages;
	coremap->c_ready = true;
}

bool
coremap_ready(void) {
	if (coremap == NULL) {
		return false;
	} else {
		return coremap->c_ready;
	}
}

paddr_t
coremap_getkpages(unsigned long npages) {	

	paddr_t paddr;
	paddr_t pgvictim;
	int c_index;
	int result;
	int incr;
	unsigned long total_npages;
	unsigned long start;
	unsigned long end;

	spinlock_acquire(&coremap->c_spinlock);
	start = 0;
	total_npages = coremap->c_npages;
	
	while (start < total_npages) {
		if (!coremap->c_entries[start].ce_allocated) {
			end = start+npages-1;
			if (end >= total_npages) {
				break;
			}

			while (end > start) {

				if (!coremap->c_entries[end].ce_allocated) {
					KASSERT(coremap->c_entries[end].ce_addrspace
					== NULL);
					KASSERT(!coremap->c_entries[end].ce_busy);
					KASSERT(coremap->c_entries[end].ce_next
					== 0);
					KASSERT(coremap->c_entries[end].ce_pgentry == NULL);
					KASSERT(coremap->c_entries[end].ce_swapoffset == -1);
				}

				if (coremap->c_entries[end].ce_allocated) {
					break;

				} else {
					end--;
				}
			}
		
			if (start == end) {
				for (unsigned i=start; i<start+npages; i++) {
					coremap->c_entries[i].ce_allocated = true;
					coremap->c_entries[i].ce_foruser =
					false;

					if (i < start+npages-1) {
						coremap->c_entries[i].ce_next = 1;
					} else {
						coremap->c_entries[i].ce_next = 0;
					}		
				}

				paddr = (paddr_t)(start*PAGE_SIZE);
				spinlock_release(&coremap->c_spinlock);
				return paddr;
			} 
					
		}

		start++;
	}

	spinlock_release(&coremap->c_spinlock);

	if (npages == 1) {

		sw_getpage(&pgvictim);

		KASSERT(!spinlock_do_i_hold(&coremap->c_spinlock));
		c_index = (int)(pgvictim/PAGE_SIZE);
		KASSERT(coremap->c_entries[c_index].ce_allocated);
		KASSERT(coremap->c_entries[c_index].ce_foruser);
		KASSERT(coremap->c_entries[c_index].ce_busy);
		KASSERT((paddr_t)((*coremap->c_entries[c_index].ce_pgentry &
		PG_FRAME) << 12) == pgvictim);
		KASSERT(*coremap->c_entries[c_index].ce_pgentry & PG_VALID);
		KASSERT(!(*coremap->c_entries[c_index].ce_pgentry & PG_SWAP));
		KASSERT(*coremap->c_entries[c_index].ce_pgentry & PG_BUSY);
		
		result = sw_evictpage(pgvictim);
		if (result) {
			spinlock_acquire(&coremap->c_spinlock);
			coremap->c_entries[c_index].ce_busy = false;
			spinlock_release(&coremap->c_spinlock);
			return result;
		}

		KASSERT(coremap->c_entries[c_index].ce_busy);

		spinlock_acquire(&coremap->c_spinlock);	

		coremap->c_entries[c_index].ce_allocated = true;
		coremap->c_entries[c_index].ce_pgentry = NULL;
		coremap->c_entries[c_index].ce_addrspace = NULL;
		coremap->c_entries[c_index].ce_foruser = false;
		coremap->c_entries[c_index].ce_next = 0;
		coremap->c_entries[c_index].ce_swapoffset = -1;
		coremap->c_entries[c_index].ce_busy = false;

		spinlock_release(&coremap->c_spinlock);

		return pgvictim;
	
	} else {

		spinlock_acquire(&coremap->c_spinlock);
		start = 0;
		incr = 0;
		total_npages = coremap->c_npages;
		
		while (start < total_npages) {

			while ((unsigned)incr < npages) {
			
				if (!coremap->c_entries[start+incr].ce_allocated) {
					coremap->c_entries[start+incr].ce_busy =
					true;

				} else if (coremap->c_entries[start+incr].ce_foruser &&
				!coremap->c_entries[start+incr].ce_busy) {
					coremap->c_entries[start+incr].ce_busy =
					true;
					spinlock_release(&coremap->c_spinlock);
					lock_acquire(coremap->c_entries[start+incr].ce_addrspace->as_lock);
					
					if(*coremap->c_entries[start+incr].ce_pgentry
					& PG_BUSY ||
					*coremap->c_entries[start+incr].ce_pgentry
					& PG_SWAP) {
						lock_release(coremap->c_entries[start+incr].ce_addrspace->as_lock);
						spinlock_acquire(&coremap->c_spinlock);
						break;

					} else {
						*coremap->c_entries[start+incr].ce_pgentry
						|= PG_BUSY;
						lock_release(coremap->c_entries[start+incr].ce_addrspace->as_lock);
						spinlock_acquire(&coremap->c_spinlock);

					}
		
				} else {
					break;

				}

				incr++;
			}

			if ((unsigned)incr < npages) {
				
				while (incr > 0) {

					KASSERT(coremap->c_entries[start+incr-1].ce_busy
					== true);
					coremap->c_entries[start+incr-1].ce_busy =
					false;

					if (coremap->c_entries[start+incr-1].ce_foruser) {
				
						spinlock_release(&coremap->c_spinlock);
						
						lock_acquire(coremap->c_entries[start+incr-1].ce_addrspace->as_lock);

						KASSERT(*coremap->c_entries[start+incr-1].ce_pgentry
						& PG_BUSY);

						*coremap->c_entries[start+incr-1].ce_pgentry
						&= ~PG_BUSY;

						cv_wait(coremap->c_entries[start+incr-1].ce_addrspace->as_cv,
						coremap->c_entries[start+incr-1].ce_addrspace->as_lock);
					
						lock_release(coremap->c_entries[start+incr-1].ce_addrspace->as_lock);

						spinlock_acquire(&coremap->c_spinlock);
							
					} 

					incr--;

				}		
	
			} else {

				KASSERT((unsigned)incr == npages);

				for (unsigned i=start; i<start+npages; i++) {
					
					if (coremap->c_entries[i].ce_foruser) {

						pgvictim =
						(paddr_t)(i*PAGE_SIZE);

						KASSERT((paddr_t)((*coremap->c_entries[i].ce_pgentry
						& PG_FRAME) << 12) == pgvictim);
						
						result = sw_evictpage(pgvictim);
						spinlock_release(&coremap->c_spinlock);
						lock_acquire(coremap->c_entries[i].ce_addrspace->as_lock);
						*coremap->c_entries[i].ce_pgentry
						&= ~PG_BUSY;
						lock_release(coremap->c_entries[i].ce_addrspace->as_lock);

						if (result) {
							goto error;
		
						}

						spinlock_acquire(&coremap->c_spinlock);
									
					} 

				}

				for (unsigned i=start; i<start+npages; i++) {
					
					coremap->c_entries[i].ce_addrspace =
					NULL;
					coremap->c_entries[i].ce_allocated =
					true;
					coremap->c_entries[i].ce_foruser =
					false;
					coremap->c_entries[i].ce_busy = false;
					coremap->c_entries[i].ce_pgentry =
					NULL;
					coremap->c_entries[i].ce_swapoffset =
					-1;

					if (i < start+npages-1) {
						coremap->c_entries[i].ce_next =
						1;
					} else {
						coremap->c_entries[i].ce_next =
						0;
					}


				}

				spinlock_release(&coremap->c_spinlock);
				paddr = (paddr_t)(start*PAGE_SIZE);
				return paddr;

			}				

			start++;
		}
	}
 
error:

	spinlock_acquire(&coremap->c_spinlock);

	for (unsigned i=start; i<start+npages; i++) {

		KASSERT(coremap->c_entries[i].ce_busy);
		coremap->c_entries[i].ce_busy = false;

	}

	spinlock_release(&coremap->c_spinlock);
	return 0;


}

int
coremap_getpage(int *pg_entry, struct addrspace *as) {	
	KASSERT(pg_entry != NULL);
	KASSERT(sw_check(pg_entry, as));

	paddr_t paddr;
	paddr_t pgvictim;
	int c_index;
	int result;
	unsigned long total_npages;
	unsigned long start;

	spinlock_acquire(&coremap->c_spinlock);
	start = 0;
	total_npages = coremap->c_npages;

	for (unsigned long i=start; i<total_npages; i++) {
		if (!coremap->c_entries[i].ce_allocated) {
			KASSERT(!coremap->c_entries[i].ce_busy);
			KASSERT(coremap->c_entries[i].ce_next == 0);
			KASSERT(coremap->c_entries[i].ce_pgentry == NULL);
			KASSERT(coremap->c_entries[i].ce_swapoffset ==
			-1);

			coremap->c_entries[i].ce_addrspace = as;
			coremap->c_entries[i].ce_allocated = true;
			coremap->c_entries[i].ce_foruser = true;
			coremap->c_entries[i].ce_next = 0;
			
			paddr = (paddr_t)((i*PAGE_SIZE) >> 12);

			*pg_entry &= ~PG_FRAME;
			*pg_entry |= paddr;
			coremap->c_entries[i].ce_pgentry = pg_entry;
			spinlock_release(&coremap->c_spinlock);
			return 0;

		}
			
	}

	spinlock_release(&coremap->c_spinlock);
//	thread_yield();
	sw_getpage(&pgvictim);

	KASSERT(!spinlock_do_i_hold(&coremap->c_spinlock));
	c_index = (int)(pgvictim/PAGE_SIZE);
	KASSERT(coremap->c_entries[c_index].ce_allocated);
	KASSERT(coremap->c_entries[c_index].ce_foruser);
	KASSERT(coremap->c_entries[c_index].ce_busy);
	KASSERT((paddr_t)((*coremap->c_entries[c_index].ce_pgentry & PG_FRAME)
	<< 12) == pgvictim);
	
	result = sw_evictpage(pgvictim);
	if (result) {
		spinlock_acquire(&coremap->c_spinlock);
		coremap->c_entries[c_index].ce_busy = false;
		spinlock_release(&coremap->c_spinlock);
		return result;
	}

	KASSERT(coremap->c_entries[c_index].ce_busy);

	spinlock_acquire(&coremap->c_spinlock);	
	paddr = pgvictim >> 12;
	*pg_entry &= ~PG_FRAME;
	*pg_entry |= paddr; 

	coremap->c_entries[c_index].ce_allocated = true;
	coremap->c_entries[c_index].ce_addrspace = as;
	coremap->c_entries[c_index].ce_foruser = true;
	coremap->c_entries[c_index].ce_next = 0;
	coremap->c_entries[c_index].ce_pgentry = pg_entry;
	coremap->c_entries[c_index].ce_swapoffset = -1;
	coremap->c_entries[c_index].ce_busy = false;

	spinlock_release(&coremap->c_spinlock);
	return 0;
}

void
coremap_freekpages(paddr_t paddr) {
	int index;

	spinlock_acquire(&coremap->c_spinlock);
	index = (int)(paddr / PAGE_SIZE);
	
	while (coremap->c_entries[index].ce_next != 0) {
		KASSERT(coremap->c_entries[index].ce_allocated);
		KASSERT(!coremap->c_entries[index].ce_foruser);
		KASSERT(!coremap->c_entries[index].ce_busy);
		KASSERT(coremap->c_entries[index].ce_pgentry == NULL);
		KASSERT(coremap->c_entries[index].ce_swapoffset == -1);
			
		coremap->c_entries[index].ce_allocated = false;
		coremap->c_entries[index].ce_next = 0;
		index++;
	}

	KASSERT(coremap->c_entries[index].ce_allocated);
	KASSERT(!coremap->c_entries[index].ce_foruser);
	KASSERT(!coremap->c_entries[index].ce_busy);
	KASSERT(coremap->c_entries[index].ce_pgentry == NULL);
	KASSERT(coremap->c_entries[index].ce_swapoffset == -1);

	coremap->c_entries[index].ce_allocated = false;
	spinlock_release(&coremap->c_spinlock);
}

void
coremap_freepage(paddr_t pframe) {
	int index;

	spinlock_acquire(&coremap->c_spinlock);
	index = (int)(pframe / PAGE_SIZE);

	KASSERT(sw_check(coremap->c_entries[index].ce_pgentry,
	coremap->c_entries[index].ce_addrspace));
	KASSERT(coremap->c_entries[index].ce_allocated);
	KASSERT(coremap->c_entries[index].ce_foruser);
	KASSERT(!coremap->c_entries[index].ce_busy);
	KASSERT(coremap->c_entries[index].ce_next == 0);
	KASSERT(coremap->c_entries[index].ce_pgentry != NULL);

	coremap->c_entries[index].ce_allocated = false;
	coremap->c_entries[index].ce_foruser = false;
	coremap->c_entries[index].ce_busy = false;
	coremap->c_entries[index].ce_next  = 0;
	coremap->c_entries[index].ce_pgentry = NULL;

	if (coremap->c_entries[index].ce_swapoffset >= 0) {
		lock_acquire(kswap->sw_disklock);
		bitmap_unmark(kswap->sw_diskoffset, (unsigned)index);
		lock_release(kswap->sw_disklock);
	}	

	coremap->c_entries[index].ce_swapoffset = -1;

	spinlock_release(&coremap->c_spinlock);
}
