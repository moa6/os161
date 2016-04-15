/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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
#include <kern/errno.h>
#include <array.h>
#include <lib.h>
#include <spl.h>
#include <spinlock.h>
#include <synch.h>
#include <proc.h>
#include <current.h>
#include <mips/tlb.h>
#include <uio.h>
#include <vnode.h>
#include <bitmap.h>
#include <swap.h>
#include <coremap.h>
#include <addrspace.h>
#include <vm.h>

/*
 * Wrap ram_stealmem in a spinlock.
 */
static struct spinlock stealmem_lock = SPINLOCK_INITIALIZER;

/*
 * vm_bootstrap
 *
 * Set up the virtual memory system during bootup
 */
void
vm_bootstrap(void)
{
	/* Set up the coremap and kernel swap structure in bootup */
	coremap_bootstrap();
	sw_bootstrap();
}

/*
 * getppages
 *
 * Uses the ram_stealmem function to retrieve free physical pages.  Should not
 * be used once the coremap is set up 
 */
static
paddr_t
getppages(unsigned long npages)
{
	paddr_t addr;

	spinlock_acquire(&stealmem_lock);

	addr = ram_stealmem(npages);

	spinlock_release(&stealmem_lock);
	
	return addr;
}

/* alloc_kpages
 *
 * Used by kmalloc to retrieve free pages
 */
vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t pa;

	if (coremap_ready()) {

		/* If the coremap is ready, we get kernel pages by looking up
		 * the coremap */

		pa = coremap_getkpages(npages);

	} else {

		/* If the coremap is not ready, we can use getppages to get new
		 * kernel pages */

		pa = getppages(npages);

	}

	if (pa == 0) {

		/* If pa equals 0, we were unable to get new kernel pages.  So
		 * we return 0 */
		return 0;
	}

	/* Return the virtual address */
	return PADDR_TO_KVADDR(pa);
}

/*
 * free_kpages
 *
 * Called by kfree to free kernel pages
 */
void
free_kpages(vaddr_t addr)
{
	paddr_t pframe;

	pframe = (paddr_t)(addr - MIPS_KSEG0);
	coremap_freekpages(pframe);
}

void
vm_tlbshootdown_all(void)
{
	panic("vm_tlbshootdown_all not used\n");
}

/*
 * vm_tlbshootdown
 *
 * Shoots down entries in the TLB
 */
void
vm_tlbshootdown(const struct tlbshootdown *ts)
{
	int i;
	int spl;
	uint32_t ehi, elo;
	paddr_t paddr = ts->ts_paddr;

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {

		/* Read a TLB entry */
		tlb_read(&ehi, &elo, i);

		if ((elo & TLBLO_PPAGE) == paddr) {
			/* If the TLB low entry contains paddr, invalidate it */
			tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
		}

	}

	splx(spl);

	/* Increment the kswap tlbshootdown semaphore to indicate that the
	 * tlbshootdown is complete */
	V(kswap->sw_tlbshootdown_sem);

}

/*
 * vm_fault
 *
 * Handles virtual memory faults
 */
int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	int tlb_index;
	int *pgtable;
	int result;
	signed long index;
	unsigned long npages;
	vaddr_t vbase1, vtop1, vbase2, vtop2, stacktop, heaptop, stacklimit;
	paddr_t paddr;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	/* Align the fault address to 4K boundaries */
	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "vm: fault: 0x%x\n", faultaddress);

	/* Check that the faulttype is valid.  If not, return EINVAL. */
	if (faulttype != VM_FAULT_READONLY && faulttype != VM_FAULT_READ &&
		faulttype != VM_FAULT_WRITE) {
		return EINVAL;
	}

	if (curproc == NULL) {
		/*
		 * No process. This is probably a kernel fault early
		 * in boot. Return EFAULT so as to panic instead of
		 * getting into an infinite faulting loop.
		 */
		return EFAULT;
	}

	as = proc_getas();
	if (as == NULL) {
		/*
		 * No address space set up. This is probably also a
		 * kernel fault early in boot.
		 */
		return EFAULT;
	}

	/* Determine the base address and top address of each address region.
	 * Note that the top address of address region 2 is the same as the base
	 * address for the heap. */
	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stacktop = USERSTACK;
	stacklimit = USERSTACK - NSTACKPAGES * PAGE_SIZE;
	heaptop = as->as_heaptop;

	/* Determine which address region the faultaddress lies and which page
	 * table entry we should be accessing. */

	if (faultaddress >= vbase1 && faultaddress < vtop1) {
		index = (signed long)((faultaddress - vbase1) / PAGE_SIZE);
		npages = as->as_npages1;
		pgtable = as->as_pgtable1;
		goto fetchpaddr;

	} else if (faultaddress >= vbase2 && faultaddress < vtop2) {
		index = (signed long)((faultaddress - vbase2) / PAGE_SIZE);
		npages = as->as_npages2;
		pgtable = as->as_pgtable2;
		goto fetchpaddr;

	} else if (faultaddress >= stacklimit && faultaddress < stacktop) {

		index = (signed long)(((stacktop - faultaddress) / PAGE_SIZE) -
		1);

		/* Adjust the stackptr if necessary */

		if (stacktop - (index+1)*PAGE_SIZE < as->as_stackptr) {
			as->as_stackptr = stacktop - (index+1)*PAGE_SIZE;
		}

		npages = (unsigned long)((stacktop - as->as_stackptr) / PAGE_SIZE);
		pgtable = as->as_stackpgtable;
		goto fetchpaddr;

	} else if (faultaddress >= vtop2 && faultaddress < heaptop) {  

		index = (signed long)((faultaddress - vtop2) / PAGE_SIZE);

		npages = (unsigned long)(((heaptop & PAGE_FRAME) - vtop2) / PAGE_SIZE);
		if (heaptop & ~PAGE_FRAME) {
			npages++;
		}

		pgtable = as->as_heappgtable;
		goto fetchpaddr;
		
	} else {
	
		/* If faultaddress does not lie in any of the address regions,
		 * return EFAULT */

		return EFAULT;
	}		
	
fetchpaddr:

	/* Now that we know which address region faultaddress belongs to, look
	 * up the matching page table entry */
	
	if (index >= (signed long)npages) {

		return EFAULT;

	} 
	
	/* Acquire the address space lock */
	lock_acquire(as->as_lock);

	/* If the page table entry is marked as busy, the physical page is being
	 * evicted.  Wait until the page table entry is not busy before
	 * proceeding. */
	while (pgtable[index] & PG_BUSY) {
		cv_wait(as->as_cv, as->as_lock);
	}

	if (pgtable[index] & PG_VALID) {

		/* If the page table entry is marked as valid, get the physical
		 * address */

		if (faulttype == VM_FAULT_READONLY && !(pgtable[index] & PG_DIRTY)) {
			pgtable[index] |= PG_DIRTY;
		}

		paddr = (paddr_t)((pgtable[index] & PG_FRAME) << 12);

	} else if (pgtable[index] & PG_SWAP) { 

		/* If the page has been swapped out, mark the page table entry
		 * as busy, release the address space lock, and perform a page
		 * in */

		pgtable[index] |= PG_BUSY;
		
		lock_release(as->as_lock);
	
		result = sw_pagein(&pgtable[index], as);
		if (result) {
			pgtable[index] &= ~PG_BUSY;
			lock_release(as->as_lock);
			return result;
		}

		/* Acquire the address space lock again */
		lock_acquire(as->as_lock);

		/* Get the physical address from the page table entry */
		paddr = (paddr_t)((pgtable[index] & PG_FRAME) << 12);
		
	} else if (pgtable[index] == 0) {

		/* If the page table entry is 0, then we need to request a new
		 * physical page.  We first mark the page table entry as being valid,
		 * dirty, and busy. We then release the address space lock and
		 * call coremap_getpage to get a new page.*/

		pgtable[index] = PG_VALID | PG_DIRTY | PG_BUSY;

		lock_release(as->as_lock);

		result = coremap_getpage(&pgtable[index], as);
		if (result) {
			pgtable[index] = 0;
			return result;
		}

		/* Re-acquire the address space lock */
		lock_acquire(as->as_lock);
		
		/* Get the physical address from the page table entry and zero
		 * any data in the new page */
		paddr = (paddr_t)((pgtable[index] & PG_FRAME) << 12);
		bzero((void *)PADDR_TO_KVADDR(paddr), PAGE_SIZE);

	} else {
		panic("vm_fault should not get to here!\n");
	}
	
	/* make sure the physical address is page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	/* Randomly select a place in the TLB to add a new TLB entry */
	tlb_index = random() % NUM_TLB;
	ehi = faultaddress;

	/* In our implementation, TLB entries are always marked as dirty */
	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;

	DEBUG(DB_VM, "vm: 0x%x -> 0x%x\n", faultaddress, paddr);

	/* Add a new TLB entry */
	tlb_write(ehi, elo, tlb_index);

	splx(spl);

	if (pgtable[index] & PG_BUSY) {

		/* If the page table entry was marked as busy, mark it as no
		 * longer being busy */
		pgtable[index] &= ~PG_BUSY;
	}

	/* Release the address space lock */
	lock_release(as->as_lock);

	return 0;
}

/*
 * as_create
 *
 * Creates and initializes the new address space
 */
struct addrspace *
as_create(void)
{
	/* Create a new address space */
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	/* Create the address space lock */
	as->as_lock = lock_create("as lock");
	if (as->as_lock == NULL) {
		return NULL;
	}

	/* Create the address space conditional variable */
	as->as_cv = cv_create("as cv");
	if (as->as_cv == NULL) {
		return NULL;
	}

	/* Initialize the rest of the address space fields.  Except for the
	 * stackptr, all of the fields are either NULL or 0 */
	as->as_pgtable1 = NULL;
	as->as_vbase1 = 0;
	as->as_npages1 = 0;
	as->as_pgtable2 = NULL;
	as->as_vbase2 = 0;
	as->as_npages2 = 0;
	as->as_stackpgtable = NULL;
	as->as_stackptr = USERSTACK;
	as->as_heappgtable = NULL;
	as->as_heaptop = 0;
	as->as_heapsz = MIN_HEAPSZ;

	/* Return the new address space */ 
	return as;
}

/*
 * as_destroyregion
 *
 * Destroys the page table for a given address region
 */
void
as_destroyregion(struct addrspace *as, int as_regiontype)
{
	KASSERT(as != NULL);

	int *pgtable;
	int npages;
	int c_index;
	unsigned sw_offset;

	/* Select the page table to destroy based on as_regiontype */

	switch (as_regiontype) {
		case AS_REGION1:
			pgtable = as->as_pgtable1;
			npages = (int)(as->as_npages1);
			break;
		case AS_REGION2:
			pgtable = as->as_pgtable2;
			npages = (int)(as->as_npages2);
			break;
		case AS_HEAP:
			pgtable = as->as_heappgtable;
			npages = (int)(as->as_heapsz);
			break;
		case AS_STACK:
			pgtable = as->as_stackpgtable;
			npages = NSTACKPAGES;
			break;
		default:
			panic("Address region unsupported\n");
	}

	if (pgtable == NULL) {

		/* If there is no page table, do nothing */
		
		return;
	}

	for (int i=0; i<npages; i++) {

		/* Go through the page table entries and either free pages or
		 * free the page contents in the swap file */

		if (pgtable[i] == 0) {

			/* If the page table entry is zero, examine the next
			 * page table entry */

			continue;

		} else {

			/* Acquire the address space lock */
			lock_acquire(as->as_lock);

			/* Wait until the page table entry is no longer marked
			 * as busy */
			while (pgtable[i] & PG_BUSY) {
				cv_wait(as->as_cv, as->as_lock);
			}

			/* Mark the page table entry as being busy */
			pgtable[i] |= PG_BUSY;

			/* Release the address space lock */
			lock_release(as->as_lock);

			/* Acquire the coremap spinlock */
			spinlock_acquire(&coremap->c_spinlock);

			if (pgtable[i] & PG_VALID) {

				/* If the page table entry is valid, we must
				 * free the user page */
				
				c_index = (int)(((pgtable[i] & PG_FRAME) << 12)
				/ PAGE_SIZE);

				/* There is a possibility that a page daemon or
				 * some other process is attempting to evict the
				 * page.  We must wait until that is no longer
				 * the case. Once we acquire the spinlock and
				 * the coremap entry is no longer marked as
				 * busy, nothing can swap out the page. */
				while (coremap->c_entries[c_index].ce_busy) {
					spinlock_release(&coremap->c_spinlock);
					thread_yield();
					spinlock_acquire(&coremap->c_spinlock);
				}

				/* Check that we have the right coremap entry */
				KASSERT((paddr_t)((*coremap->c_entries[c_index].ce_pgentry
				& PG_FRAME) << 12) ==
				(paddr_t)(c_index*PAGE_SIZE));

				/* Check that the page table entry is no longer
				 * marked as busy */
				KASSERT(!coremap->c_entries[c_index].ce_busy);

				/* Free the physical page */
				coremap->c_entries[c_index].ce_addrspace = NULL;
				coremap->c_entries[c_index].ce_allocated =
				false;
				coremap->c_entries[c_index].ce_foruser = true;
				coremap->c_entries[c_index].ce_next = 0;
				coremap->c_entries[c_index].ce_pgentry = NULL;
				coremap->c_entries[c_index].ce_swapoffset = -1;

				/* Release the coremap spinlock */
				spinlock_release(&coremap->c_spinlock);

			} else if (pgtable[i] & PG_SWAP) {
			
				/* If the page table entry is marked as swap, we
				 * simply mark the offset location in the swap
				 * file as free */

				spinlock_release(&coremap->c_spinlock);	
				sw_offset = (unsigned)(pgtable[i] & PG_FRAME);
				lock_acquire(kswap->sw_disklock);
				bitmap_unmark(kswap->sw_diskoffset, sw_offset);
				lock_release(kswap->sw_disklock);
			
			} else {
				panic("as_destroy should not get to here\n");
			}

		}

	}
}
/*
 * as_destroy
 *
 * Destroys the address space
 */
void
as_destroy(struct addrspace *as) {
	
	/*Destroy the page tables for all address regions */
	as_destroyregion(as, AS_REGION1);
	as_destroyregion(as, AS_REGION2);
	as_destroyregion(as, AS_HEAP);
	as_destroyregion(as, AS_STACK);
	
	/* Destroy the address space lock */
	lock_destroy(as->as_lock);
	
	/* Destroy the address space cv */
	cv_destroy(as->as_cv);

	/* Free the address space */
	kfree(as);
}

/*
 * as_activate
 *
 * Activates the address space
 */
void
as_activate(void)
{
	int i, spl;
	struct addrspace *as;

	as = proc_getas();
	if (as == NULL) {
		return;
	}

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	for (i=0; i<NUM_TLB; i++) {

		/* Invalidate the TLB entry */

		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}

/*
 * as_define_region
 *
 * Defines the address region
 */
int
as_define_region(struct addrspace *as, vaddr_t vaddr, size_t sz,
		 int readable, int writeable, int executable)
{
	size_t npages;

	/* Align the region. First, the base... */
	sz += vaddr & ~(vaddr_t)PAGE_FRAME;
	vaddr &= PAGE_FRAME;

	/* ...and now the length. */
	sz = (sz + PAGE_SIZE - 1) & PAGE_FRAME;

	npages = sz / PAGE_SIZE;

	/* We don't use these - all pages are read-write */
	(void)readable;
	(void)writeable;
	(void)executable;

	if (as->as_vbase1 == 0) {

		/* If address region 1 has not yet been defined, define it */

		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	} 
	
	if (as->as_vbase2 == 0) {

		/* If address region 2 has not yet been defined, define it */

		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		as->as_heaptop = as->as_vbase2 + as->as_npages2 * PAGE_SIZE;
		return 0;
	}

	panic("vm does not support more than two regions!\n");
}

/*
 * as_prepare_load
 *
 * Called before actually loading from an executable into the address space
 */
int
as_prepare_load(struct addrspace *as)
{

	/* Create the page table for address region 1 */
	as->as_pgtable1 = kmalloc(as->as_npages1 * sizeof(int));
	if (as->as_pgtable1 == NULL) {
		as_destroy(as);
		return ENOMEM;
	}

	/* Initialize the page table entries for the address region 1 page table
	 */
	for (unsigned long i=0; i<as->as_npages1; i++) {
		as->as_pgtable1[i] = 0;
	}

	/* Create the page table for address region 2 */
	as->as_pgtable2 = kmalloc(as->as_npages2 * sizeof(int));
	if (as->as_pgtable2 == NULL) {
		as_destroy(as);
		return ENOMEM;
	}

	/* Initialize the page table entries for the address region 2 page table
	 */
	for (unsigned long i=0; i<as->as_npages2; i++) {
		as->as_pgtable2[i] = 0;
	}
	
	return 0;	
}

/*
 * as_complete_load
 *
 * Called when loading from an executable is complete.  In our implementation,
 * it does nothing.
 */
int
as_complete_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

/*
 * as_define_stack
 *
 * Defines the stack region in the address space
 */
int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	*stackptr = as->as_stackptr;

	/* Create the stack page table */
	as->as_stackpgtable = kmalloc(NSTACKPAGES * sizeof(int));
	if (as->as_stackpgtable == NULL) {
		as_destroy(as);
		return ENOMEM;
	}

	/* Initialize the page table entries in the stack page table */
	for (unsigned long i=0; i<NSTACKPAGES; i++) {
		as->as_stackpgtable[i] = 0;
	}

	return 0;
}

/*
 * as_growheap
 *
 * Doubles the size of the heap region
 */
int
as_growheap(struct addrspace *as) {

	int *heap_temp;
	int c_index;
	paddr_t paddr;

	/* Create heap_temp which is double the size of the current heap page
	 * table */
	heap_temp = kmalloc(2*as->as_heapsz*sizeof(int));
	if (heap_temp == NULL) {
		return ENOMEM;
	}

	/* Initialize the page table entries in heap_temp */
	for (int i=0; i<2*as->as_heapsz; i++) {
		heap_temp[i] = 0;
	}

	/* Copy the current heap page table over to heap_temp */ 

	for (int i=0; i<as->as_heapsz; i++) {

		/* Acquire the address space lock */
		lock_acquire(as->as_lock);

		/* Wait until the page table entry is no longer marked as busy
		 */
		while (as->as_heappgtable[i] & PG_BUSY) {
			cv_wait(as->as_cv, as->as_lock);
		}

		/* Mark the page table entry as being busy */
		as->as_heappgtable[i] |= PG_BUSY;

		/* Release the address space lock */
		lock_release(as->as_lock);
		
		if (as->as_heappgtable[i] & PG_VALID) {

			/* If the page table entry is marked as valid, copy the
			 * page table entry contents from the current heap page
			 * table to heap_temp.  Also, make sure
			 * that ce_pgentry of the corresponding coremap entry is
			 * pointing to the page table entry in heap_temp */

			paddr = (paddr_t)((as->as_heappgtable[i]
			& PG_FRAME) << 12);
			c_index = (int)(paddr/PAGE_SIZE);
			spinlock_acquire(&coremap->c_spinlock);
			KASSERT(coremap->c_entries[c_index].ce_addrspace
			== as);
			KASSERT(coremap->c_entries[c_index].ce_pgentry
			== &as->as_heappgtable[i]);
			memcpy(&heap_temp[i],
			&as->as_heappgtable[i], sizeof(int));
			coremap->c_entries[c_index].ce_pgentry =
			&heap_temp[i];
			spinlock_release(&coremap->c_spinlock);
				
		} else if (as->as_heappgtable[i] & PG_SWAP) {

			/* If the page table entry is marked as swap, just copy
			 * the page table entry contents from the current heap
			 * page table to heap_temp */

			memcpy(&heap_temp[i],
			&as->as_heappgtable[i], sizeof(int));

		} else {
			KASSERT(as->as_heappgtable[i] == PG_BUSY);
		}

		/* Because we marked the page table entry of the original heap
		 * page table as being busy and copied the entry contents over
		 * to heap_temp, we must now mark the page table entry in
		 * heap_temp as no longer busy */
		heap_temp[i] &= ~PG_BUSY;

		/* We acquire the address space lock, mark the page table entry
		 * in the original heap page table as no longer being busy, and
		 * release the address space lock */

		lock_acquire(as->as_lock);
		as->as_heappgtable[i] &= ~PG_BUSY;
		cv_signal(as->as_cv, as->as_lock);
		lock_release(as->as_lock);

	}

	/* Free the original heap page table */
	kfree(as->as_heappgtable);
	
	/* Set heap_temp as the new heap page table */
	as->as_heappgtable = heap_temp;

	/* Indicate that the size of the heap page table is now doubled */
	as->as_heapsz *= 2;

	return 0;
}

/*
 * as_shrinkheap
 *
 * Decreases the size of the heap page table by half.  Works similarly to
 * as_growheap.
 */
int
as_shrinkheap(struct addrspace *as) {

	int *heap_temp;
	int c_index;
	paddr_t paddr;

	if (as->as_heapsz == MIN_HEAPSZ) {
		return 0;
	}

	heap_temp = kmalloc(as->as_heapsz/2*sizeof(int));
	if (heap_temp == NULL) {
		return ENOMEM;
	}

	for (int i=0; i<as->as_heapsz/2; i++) {
		heap_temp[i] = 0;
	}

	for (int i=0; i<as->as_heapsz/2; i++) {

		lock_acquire(as->as_lock);
		while (as->as_heappgtable[i] & PG_BUSY) {
			cv_wait(as->as_cv, as->as_lock);
		}
		as->as_heappgtable[i] |= PG_BUSY;
		lock_release(as->as_lock);
		
		if (as->as_heappgtable[i] & PG_VALID) {
			paddr = (paddr_t)((as->as_heappgtable[i]
			& PG_FRAME) << 12);
			c_index = (int)(paddr/PAGE_SIZE);
			spinlock_acquire(&coremap->c_spinlock);
			KASSERT(coremap->c_entries[c_index].ce_addrspace
			== as);
			KASSERT(coremap->c_entries[c_index].ce_pgentry
			== &as->as_heappgtable[i]);
			memcpy(&heap_temp[i],
			&as->as_heappgtable[i], sizeof(int));
			coremap->c_entries[c_index].ce_pgentry =
			&heap_temp[i];
			spinlock_release(&coremap->c_spinlock);
				
		} else if (as->as_heappgtable[i] & PG_SWAP) {
			memcpy(&heap_temp[i],
			&as->as_heappgtable[i], sizeof(int));

		} else {
			KASSERT(as->as_heappgtable[i] == PG_BUSY);
		}

		heap_temp[i] &= ~PG_BUSY;

		lock_acquire(as->as_lock);
		as->as_heappgtable[i] &= ~PG_BUSY;
		cv_signal(as->as_cv, as->as_lock);
		lock_release(as->as_lock);

	}

	kfree(as->as_heappgtable);
	as->as_heappgtable = heap_temp;
	as->as_heapsz /= 2;
	return 0;
}

/*
 * as_copyregion
 *
 * Copies the page table of a given address region
 */
int
as_copyregion(struct addrspace *old, struct addrspace *new, int as_regiontype) {
	
	int *old_pgtable;
	int *new_pgtable;
	int old_npages;
	paddr_t old_paddr;
	paddr_t new_paddr;
	int result;
	off_t sw_offset;
	struct uio ku;
	struct iovec iov;

	/* Determine which page table needs to be copied and create a new page
	 * table in the new address space */
	switch (as_regiontype) {
		case AS_REGION1:
			old_pgtable = old->as_pgtable1;
			old_npages = (int)(old->as_npages1);

			new->as_pgtable1 = kmalloc(old_npages * sizeof(int));
			if (new->as_pgtable1 == NULL) {
				return ENOMEM;
			}

			new_pgtable = new->as_pgtable1;
			break;
		case AS_REGION2:
			old_pgtable = old->as_pgtable2;
			old_npages = (int)(old->as_npages2);

			new->as_pgtable2 = kmalloc(old_npages * sizeof(int));
			if (new->as_pgtable2 == NULL) {
				return ENOMEM;
			}

			new_pgtable = new->as_pgtable2;
			break;
		case AS_HEAP:
			old_pgtable = old->as_heappgtable;
			old_npages = (int)(old->as_heapsz);

			if (old_pgtable == NULL) {

				/* If there is no heap page table in the old
				 * address space, then we simply return 0 */

				return 0;
			}

			new->as_heappgtable = kmalloc(old_npages * sizeof(int));
			if (new->as_heappgtable == NULL) {
				return ENOMEM;
			}

			new_pgtable = new->as_heappgtable;
			break;
		case AS_STACK:
			old_pgtable = old->as_stackpgtable;
			old_npages = NSTACKPAGES;

			new->as_stackpgtable = kmalloc(old_npages * sizeof(int));
			if (new->as_stackpgtable == NULL) {
				return ENOMEM;
			}

			new_pgtable = new->as_stackpgtable;
			break;
		default:
			panic("Address region unsupported\n");
	}

	/* Initialize the page table in the new address space */
	for (int i=0; i<old_npages; i++) {
		new_pgtable[i] = 0;
	}


	/* Go through each page table entry in the old address space */
	for (int i=0; i<old_npages; i++) {

		/* Acquire the old address space lock */
		lock_acquire(old->as_lock);

		/* Wait until the old page table entry is no longer marked as busy
		 */
		while (old_pgtable[i] & PG_BUSY) {
			cv_wait(old->as_cv, old->as_lock);
		}

		/* Mark the old page table entry as busy */
		old_pgtable[i] |= PG_BUSY;

		/* Release the old address space lock */
		lock_release(old->as_lock);

		if (old_pgtable[i] & PG_VALID) {

			/* If the old page table entry is marked as valid, get a
			 * new page for the new page table entry */

			old_paddr = (paddr_t)((old_pgtable[i] & PG_FRAME) << 12);

			/* Mark the new page table entry as being valid, busy,
			 * and dirty.  We mark the new page table entry as being
			 * busy so that the new page does not get evicted too soon. */
			new_pgtable[i] = PG_VALID | PG_BUSY | PG_DIRTY;

			/* Get a new page for the new page table entry */
			result = coremap_getpage(&new_pgtable[i], new);
			if (result) {
				lock_acquire(old->as_lock);
				old_pgtable[i] &= ~PG_BUSY;
				cv_signal(old->as_cv, old->as_lock);
				lock_release(old->as_lock);
				as_destroy(new);
				return result;
			}

			new_paddr = (paddr_t)((new_pgtable[i] & PG_FRAME) << 12);

			/* Copy the contents of the old page into the new page
			 */
			memmove((void *)PADDR_TO_KVADDR(new_paddr), (const void
			*)PADDR_TO_KVADDR(old_paddr), PAGE_SIZE);
			
			/* Acquire the new address space lock, mark the new page
			 * table entry as not busy, and release the new address
			 * space lock */
			lock_acquire(new->as_lock);
			new_pgtable[i] &= ~PG_BUSY;
			lock_release(new->as_lock);
			
		} else if (old_pgtable[i] & PG_SWAP) {

			/* If the old page table entry is marked as swap, the
			 * contents are in the swap file */

			/* Mark the new page table entry as being valid, busy,
			 * and dirty.  We mark the new page table entry as being
			 * busy so the new page does not get evicted too soon */
			new_pgtable[i] = PG_VALID | PG_BUSY | PG_DIRTY;

			/* Get a new page for the new page table entry */
			result = coremap_getpage(&new_pgtable[i], new);
			if (result) {
				lock_acquire(old->as_lock);
				old_pgtable[i] &= ~PG_BUSY;
				cv_signal(old->as_cv, old->as_lock);
				lock_release(old->as_lock);
				as_destroy(new);
				return result;
			}

			/* Get the address of the new physical page */
			new_paddr = (paddr_t)((new_pgtable[i] & PG_FRAME) << 12);

			/* Get the offset location of the page data stored in
			 * the swap file.  This information is contained in the
			 * old page table entry. */
			sw_offset = (off_t)((old_pgtable[i] & PG_FRAME) *
			PAGE_SIZE);

			/* Read the contents of the data from the swap file to
			 * the new physical page */
			uio_kinit(&iov, &ku, (void *)PADDR_TO_KVADDR(new_paddr), PAGE_SIZE,
			sw_offset, UIO_READ);
			result = kswap->sw_vn->vn_ops->vop_read(kswap->sw_vn, &ku);
			if (result) {
				lock_acquire(old->as_lock);
				old_pgtable[i] &= ~PG_BUSY;
				cv_signal(old->as_cv, old->as_lock);
				lock_release(old->as_lock);
				as_destroy(new);
				return result;
			}

			/* Mark the new page table entry as not busy */
			lock_acquire(new->as_lock);
			new_pgtable[i] &= ~PG_BUSY;
			lock_release(new->as_lock);
		
		} else {
			KASSERT(old_pgtable[i] == PG_BUSY);
		}

		/* Mark the old page table entry as not being busy */ 
		lock_acquire(old->as_lock);
		old_pgtable[i] &= ~PG_BUSY;
		lock_release(old->as_lock);

	}

	return 0;
}

/*
 * as_copy
 *
 * Copies the contents of an address space into a new one
 */
int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	int result;
	struct addrspace *new;

	/* Create a new address space */
	new = as_create();
	if (new == NULL) {
		return ENOMEM;
	}

	/* Copy the fields in the old address space over to the new address
	 * space */
	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;
	new->as_stackptr = old->as_stackptr;
	new->as_heaptop = old->as_heaptop;
	new->as_heapsz = old->as_heapsz;

	/* Use as_copyregion to copy the page table of each region to the new
	 * address space */

	result = as_copyregion(old, new, AS_REGION1);
	if (result) {
		return result;
	}

	result = as_copyregion(old, new, AS_REGION2);
	if (result) {
		return result;
	}

	result = as_copyregion(old, new, AS_STACK);
	if (result) {
		return result;
	}

	result = as_copyregion(old, new, AS_HEAP);
	if (result) {
		return result;
	}

	*ret = new;
	return 0;

}
