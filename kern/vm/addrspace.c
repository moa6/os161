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

void
vm_bootstrap(void)
{
	coremap_bootstrap();
	sw_bootstrap();
}

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

/* Allocate/free some kernel-space virtual pages */
vaddr_t
alloc_kpages(unsigned npages)
{
	paddr_t pa;

	if (coremap_ready()) {
		pa = coremap_getkpages(npages);

	} else {
		pa = getppages(npages);

	}

	if (pa == 0) {
		return 0;
	}

	return PADDR_TO_KVADDR(pa);
}

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
	panic("vm tried to do tlb shootdown?!\n");
}

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
		tlb_read(&ehi, &elo, i);
		if ((elo & TLBLO_PPAGE) == paddr) {
			tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
		}

	}

	splx(spl);


}

int
vm_fault(int faulttype, vaddr_t faultaddress)
{
	int tlb_index;
	int *pgtable;
	int result;
//	unsigned lru_index;
	int c_index;
	signed long index;
	unsigned long npages;
	vaddr_t vbase1, vtop1, vbase2, vtop2, stacktop, heaptop, stacklimit;
	paddr_t paddr;
	uint32_t ehi, elo;
	struct addrspace *as;
	int spl;

	faultaddress &= PAGE_FRAME;

	DEBUG(DB_VM, "vm: fault: 0x%x\n", faultaddress);

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

	vbase1 = as->as_vbase1;
	vtop1 = vbase1 + as->as_npages1 * PAGE_SIZE;
	vbase2 = as->as_vbase2;
	vtop2 = vbase2 + as->as_npages2 * PAGE_SIZE;
	stacktop = USERSTACK;
	stacklimit = USERSTACK - STACKSIZE * PAGE_SIZE;
	heaptop = as->as_heaptop;

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
		return EFAULT;
	}		
	
fetchpaddr:
	
	if (index >= (signed long)npages) {
		return EFAULT;

	} 
	
	lock_acquire(as->as_lock);

	while (pgtable[index] & PG_BUSY) {
		cv_wait(as->as_cv, as->as_lock);
	}

	if (pgtable[index] & PG_VALID) {

		if (faulttype == VM_FAULT_READONLY && !(pgtable[index] & PG_DIRTY)) {
			pgtable[index] |= PG_DIRTY;
		}

		paddr = (paddr_t)((pgtable[index] & PG_FRAME) << 12);

	} else if (pgtable[index] & PG_SWAP) { 

		pgtable[index] |= PG_BUSY;
		
		lock_release(as->as_lock);
	
		result = sw_pagein(&pgtable[index], as);
		if (result) {
			pgtable[index] &= ~PG_BUSY;
			lock_release(as->as_lock);
			return result;
		}

		lock_acquire(as->as_lock);

		paddr = (paddr_t)((pgtable[index] & PG_FRAME) << 12);
		
	} else if (pgtable[index] == 0) {
		pgtable[index] = PG_VALID | PG_DIRTY | PG_BUSY;

		lock_release(as->as_lock);

		result = coremap_getpage(&pgtable[index], as);
		if (result) {
			pgtable[index] = 0;
			return result;
		}

		lock_acquire(as->as_lock);
		
		paddr = (paddr_t)((pgtable[index] & PG_FRAME) << 12);
		c_index = (int)(paddr/PAGE_SIZE);
		KASSERT(sw_check(coremap->c_entries[c_index].ce_pgentry,
		coremap->c_entries[c_index].ce_addrspace));
		bzero((void *)PADDR_TO_KVADDR(paddr), PAGE_SIZE);

	} else {
		panic("vm_fault should not get to here!\n");
	}
	
	/* make sure it's page-aligned */
	KASSERT((paddr & PAGE_FRAME) == paddr);

	/* Disable interrupts on this CPU while frobbing the TLB. */
	spl = splhigh();

	tlb_index = random() % NUM_TLB;
	ehi = faultaddress;
/*
	if (faulttype == VM_FAULT_READONLY) {
		ts = kmalloc(sizeof(const struct tlbshootdown));
		if (ts == NULL) {
			return ENOMEM;
		}

		ts->ts_paddr = paddr;
		vm_tlbshootdown(ts);
		kfree(ts);
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;

	} else {
		elo = paddr | TLBLO_DIRTY | TLBLO_VALID;
	}
*/

	elo = paddr | TLBLO_DIRTY | TLBLO_VALID;

	DEBUG(DB_VM, "vm: 0x%x -> 0x%x\n", faultaddress, paddr);
	tlb_write(ehi, elo, tlb_index);
	splx(spl);

/*
	lru_index = (unsigned)(paddr/PAGE_SIZE);

	spinlock_acquire(&kswap->sw_lruclk->l_spinlock);
	if (!bitmap_isset(kswap->sw_lruclk->l_clkface, lru_index)) {
		bitmap_mark(kswap->sw_lruclk->l_clkface, lru_index);
	} 
	spinlock_release(&kswap->sw_lruclk->l_spinlock);	
*/

	if (pgtable[index] & PG_BUSY) {
		pgtable[index] &= ~PG_BUSY;
	}

	lock_release(as->as_lock);
	return 0;
}

struct addrspace *
as_create(void)
{
	struct addrspace *as = kmalloc(sizeof(struct addrspace));
	if (as==NULL) {
		return NULL;
	}

	as->as_lock = lock_create("as lock");
	if (as->as_lock == NULL) {
		return NULL;
	}

	as->as_cv = cv_create("as cv");
	if (as->as_cv == NULL) {
		return NULL;
	}

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

	return as;
}

void
as_destroyregion(struct addrspace *as, int as_regiontype)
{
	KASSERT(as != NULL);

	int *pgtable;
	int npages;
	int c_index;
	unsigned sw_offset;

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

			if (pgtable == NULL) {
				return;
			}

			break;
		case AS_STACK:
			pgtable = as->as_stackpgtable;
			npages = STACKSIZE;
			break;
		default:
			panic("Address region unsupported\n");
	}

	for (int i=0; i<npages; i++) {

		if (pgtable[i] == 0) {
			continue;

		} else {

			lock_acquire(as->as_lock);

			while (pgtable[i] & PG_BUSY) {
				cv_wait(as->as_cv, as->as_lock);
			}

			pgtable[i] |= PG_BUSY;

			lock_release(as->as_lock);

			spinlock_acquire(&coremap->c_spinlock);

			if (pgtable[i] & PG_VALID) {
				
				c_index = (int)(((pgtable[i] & PG_FRAME) << 12)
				/ PAGE_SIZE);

				while (coremap->c_entries[c_index].ce_busy) {
					spinlock_release(&coremap->c_spinlock);
					thread_yield();
					spinlock_acquire(&coremap->c_spinlock);
				}

				KASSERT((paddr_t)((*coremap->c_entries[c_index].ce_pgentry
				& PG_FRAME) << 12) ==
				(paddr_t)(c_index*PAGE_SIZE));

				KASSERT(!coremap->c_entries[c_index].ce_busy);
				coremap->c_entries[c_index].ce_addrspace = NULL;
				coremap->c_entries[c_index].ce_allocated =
				false;
				coremap->c_entries[c_index].ce_foruser = false;
				coremap->c_entries[c_index].ce_next = 0;
				coremap->c_entries[c_index].ce_pgentry = NULL;
				coremap->c_entries[c_index].ce_swapoffset = -1;

			} else if (pgtable[i] & PG_SWAP) {
				
				sw_offset = (unsigned)(pgtable[i] & PG_FRAME);
				lock_acquire(kswap->sw_disklock);
				bitmap_unmark(kswap->sw_diskoffset, sw_offset);
				lock_release(kswap->sw_disklock);
			
			} else {
				panic("as_destroy should not get to here\n");
			}

			spinlock_release(&coremap->c_spinlock);

		}

	}
/*		
		spinlock_acquire(&coremap->c_spinlock);
		if (pgtable[i] & PG_VALID) {
			c_index = ((pgtable[i] & PG_FRAME) << 12) / PAGE_SIZE;

			if (!coremap->c_entries[c_index].ce_busy) {
				coremap->c_entries[c_index].ce_addrspace = NULL;
				coremap->c_entries[c_index].ce_allocated = false;
				coremap->c_entries[c_index].ce_foruser = false;
				coremap->c_entries[c_index].ce_busy = false;
				coremap->c_entries[c_index].ce_next = 0;
				coremap->c_entries[c_index].ce_pgentry = NULL;
				coremap->c_entries[c_index].ce_swapoffset = -1;
			}

			spinlock_release(&coremap->c_spinlock);

		} else if (pgtable[i] & PG_SWAP) {
			spinlock_release(&coremap->c_spinlock);
			sw_offset = (unsigned)(pgtable[i] & PG_FRAME);
			lock_acquire(kswap->sw_disklock);
			bitmap_unmark(kswap->sw_diskoffset, sw_offset);
			lock_release(kswap->sw_disklock);

		} else {
			KASSERT(pgtable[i] == 0);
			spinlock_release(&coremap->c_spinlock);
		}
				
	}

	kfree(pgtable);
*/
}

void
as_destroy(struct addrspace *as) {
	
	as_destroyregion(as, AS_REGION1);
	as_destroyregion(as, AS_REGION2);
	as_destroyregion(as, AS_HEAP);
	as_destroyregion(as, AS_STACK);
	
	lock_destroy(as->as_lock);
	cv_destroy(as->as_cv);
	kfree(as);


/*
	paddr_t pframe;
	unsigned sw_offset;

	if (as->as_pgtable1 != NULL) {

		for (unsigned long i=0; i<as->as_npages1; i++) {

			lock_acquire(as->as_lock);

			while (as->as_pgtable1[i] & PG_BUSY) {
				cv_wait(as->as_cv, as->as_lock);
			}

			as->as_pgtable1[i] |= PG_BUSY;
			
			lock_release(as->as_lock);

			if (as->as_pgtable1[i] & PG_VALID) {
				pframe = (paddr_t)(as->as_pgtable1[i] << 12);
				bzero((void *)PADDR_TO_KVADDR(pframe), PAGE_SIZE);
				coremap_freepage(pframe);
							
			} else if (as->as_pgtable1[i] & PG_SWAP) {

				sw_offset = (unsigned)(as->as_pgtable1[i] &
				PG_FRAME);
				lock_acquire(kswap->sw_disklock);
				bitmap_unmark(kswap->sw_diskoffset, sw_offset);
				lock_release(kswap->sw_disklock);					

			} else {
				KASSERT(as->as_pgtable1[i] & PG_BUSY);

			}

		}

		kfree(as->as_pgtable1);
	}

	if (as->as_pgtable2 != NULL) {

		for (unsigned long i=0; i<as->as_npages2; i++) {

			lock_acquire(as->as_lock);

			while (as->as_pgtable2[i] & PG_BUSY) {
				cv_wait(as->as_cv, as->as_lock);
			}

			as->as_pgtable2[i] |= PG_BUSY;
			
			lock_release(as->as_lock);

			if (as->as_pgtable2[i] & PG_VALID) {
				pframe = (paddr_t)(as->as_pgtable2[i] << 12);
				bzero((void *)PADDR_TO_KVADDR(pframe), PAGE_SIZE);
				coremap_freepage(pframe);
							
			} else if (as->as_pgtable2[i] & PG_SWAP) {

				sw_offset = (unsigned)(as->as_pgtable2[i] &
				PG_FRAME);
				lock_acquire(kswap->sw_disklock);
				bitmap_unmark(kswap->sw_diskoffset, sw_offset);
				lock_release(kswap->sw_disklock);					

			} else {
				KASSERT(as->as_pgtable2[i] & PG_BUSY);

			}

		}

		kfree(as->as_pgtable2);
	}

	if (as->as_heappgtable != NULL) {

		for (int i=0; i<as->as_heapsz; i++) {

			lock_acquire(as->as_lock);

			while (as->as_heappgtable[i] & PG_BUSY) {
				cv_wait(as->as_cv, as->as_lock);
			}

			as->as_heappgtable[i] |= PG_BUSY;
			
			lock_release(as->as_lock);

			if (as->as_heappgtable[i] & PG_VALID) {
				pframe = (paddr_t)(as->as_heappgtable[i] << 12);
				bzero((void *)PADDR_TO_KVADDR(pframe), PAGE_SIZE);
				coremap_freepage(pframe);
							
			} else if (as->as_heappgtable[i] & PG_SWAP) {

				sw_offset = (unsigned)(as->as_heappgtable[i] &
				PG_FRAME);
				lock_acquire(kswap->sw_disklock);
				bitmap_unmark(kswap->sw_diskoffset, sw_offset);
				lock_release(kswap->sw_disklock);					

			} else {
				KASSERT(as->as_heappgtable[i] & PG_BUSY);

			}

		}

		kfree(as->as_heappgtable);
	}

	if (as->as_stackpgtable != NULL) {

		for (int i=0; i<STACKSIZE; i++) {

			lock_acquire(as->as_lock);

			while (as->as_stackpgtable[i] & PG_BUSY) {
				cv_wait(as->as_cv, as->as_lock);
			}

			as->as_stackpgtable[i] |= PG_BUSY;
			
			lock_release(as->as_lock);

			if (as->as_stackpgtable[i] & PG_VALID) {
				pframe = (paddr_t)(as->as_stackpgtable[i] << 12);
				bzero((void *)PADDR_TO_KVADDR(pframe), PAGE_SIZE);
				coremap_freepage(pframe);
							
			} else if (as->as_stackpgtable[i] & PG_SWAP) {

				sw_offset = (unsigned)(as->as_stackpgtable[i] &
				PG_FRAME);
				lock_acquire(kswap->sw_disklock);
				bitmap_unmark(kswap->sw_diskoffset, sw_offset);
				lock_release(kswap->sw_disklock);					

			} else {
				KASSERT(as->as_stackpgtable[i] & PG_BUSY);

			}

		}

		kfree(as->as_stackpgtable);
	}

	lock_destroy(as->as_lock);
	cv_destroy(as->as_cv);
	kfree(as);
*/
}

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
		tlb_write(TLBHI_INVALID(i), TLBLO_INVALID(), i);
	}

	splx(spl);
}

void
as_deactivate(void)
{
	/* nothing */
}


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
		as->as_vbase1 = vaddr;
		as->as_npages1 = npages;
		return 0;
	} 
	
	if (as->as_vbase2 == 0) {
		as->as_vbase2 = vaddr;
		as->as_npages2 = npages;
		as->as_heaptop = as->as_vbase2 + as->as_npages2 * PAGE_SIZE;
		return 0;
	}

	panic("vm does not support more than two regions!\n");
}

int
as_prepare_load(struct addrspace *as)
{

	as->as_pgtable1 = kmalloc(as->as_npages1 * sizeof(int));
	if (as->as_pgtable1 == NULL) {
		as_destroy(as);
		return ENOMEM;
	}

	for (unsigned long i=0; i<as->as_npages1; i++) {
		as->as_pgtable1[i] = 0;
	}

	as->as_pgtable2 = kmalloc(as->as_npages2 * sizeof(int));
	if (as->as_pgtable2 == NULL) {
		as_destroy(as);
		return ENOMEM;
	}

	for (unsigned long i=0; i<as->as_npages2; i++) {
		as->as_pgtable2[i] = 0;
	}

	as->as_stackpgtable = kmalloc(STACKSIZE * sizeof(int));
	if (as->as_stackpgtable == NULL) {
		as_destroy(as);
		return ENOMEM;
	}

	for (unsigned long i=0; i<STACKSIZE; i++) {
		as->as_stackpgtable[i] = 0;
	}
		
	return 0;	
}

int
as_complete_load(struct addrspace *as)
{
	(void)as;
	return 0;
}

int
as_define_stack(struct addrspace *as, vaddr_t *stackptr)
{
	*stackptr = as->as_stackptr;
	return 0;
}

int
as_growheap(struct addrspace *as) {

	int *heap_temp;
	int c_index;
	paddr_t paddr;

	heap_temp = kmalloc(2*as->as_heapsz*sizeof(int));
	if (heap_temp == NULL) {
		return ENOMEM;
	}

	for (int i=0; i<2*as->as_heapsz; i++) {
		heap_temp[i] = 0;
	}

	for (int i=0; i<as->as_heapsz; i++) {

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
	as->as_heapsz *= 2;
	return 0;
}

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
			old_npages = STACKSIZE;

			new->as_stackpgtable = kmalloc(old_npages * sizeof(int));
			if (new->as_stackpgtable == NULL) {
				return ENOMEM;
			}

			new_pgtable = new->as_stackpgtable;
			break;
		default:
			panic("Address region unsupported\n");
	}

	for (int i=0; i<old_npages; i++) {
		new_pgtable[i] = 0;
	}

	for (int i=0; i<old_npages; i++) {
		lock_acquire(old->as_lock);
		while (old_pgtable[i] & PG_BUSY) {
			cv_wait(old->as_cv, old->as_lock);
		}
		old_pgtable[i] |= PG_BUSY;
		lock_release(old->as_lock);


		if (old_pgtable[i] & PG_VALID) {

			old_paddr = (paddr_t)((old_pgtable[i] & PG_FRAME) << 12);

			new_pgtable[i] = PG_VALID | PG_BUSY | PG_DIRTY;

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

			memmove((void *)PADDR_TO_KVADDR(new_paddr), (const void
			*)PADDR_TO_KVADDR(old_paddr), PAGE_SIZE);
			
			lock_acquire(new->as_lock);
			new_pgtable[i] &= ~PG_BUSY;
			lock_release(new->as_lock);
			
		} else if (old_pgtable[i] & PG_SWAP) {

			new_pgtable[i] = PG_VALID | PG_BUSY | PG_DIRTY;

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

			sw_offset = (off_t)((old_pgtable[i] & PG_FRAME) *
			PAGE_SIZE);
			uio_kinit(&iov, &ku, (void *)PADDR_TO_KVADDR(new_paddr), PAGE_SIZE,
			sw_offset, UIO_READ);
			result = VOP_READ(kswap->sw_file, &ku);
			if (result) {
				lock_acquire(old->as_lock);
				old_pgtable[i] &= ~PG_BUSY;
				cv_signal(old->as_cv, old->as_lock);
				lock_release(old->as_lock);
				as_destroy(new);
				return result;
			}

			lock_acquire(new->as_lock);
			new_pgtable[i] &= ~PG_BUSY;
			lock_release(new->as_lock);
		
		} else {
			KASSERT(old_pgtable[i] == PG_BUSY);
		}


		lock_acquire(old->as_lock);
		old_pgtable[i] &= ~PG_BUSY;
		lock_release(old->as_lock);

	}
/*
	lock_acquire(old->as_lock);
	for (int i=0; i<old_npages; i++) {

		if (old_pgtable[i] != 0) {
			KASSERT(old_pgtable[i] & PG_BUSY);
			old_pgtable[i] &= ~PG_BUSY;
		}

	}
	lock_release(old->as_lock);

	lock_acquire(new->as_lock);
	for (int i=0; i<old_npages; i++) {

		if (new_pgtable[i] != 0) {
			KASSERT(new_pgtable[i] & PG_BUSY);
			new_pgtable[i] &= ~PG_BUSY;
		}

	}
	lock_release(new->as_lock);
*/
	return 0;
}

int
as_copy(struct addrspace *old, struct addrspace **ret)
{
	int result;
	struct addrspace *new;

	new = as_create();
	if (new == NULL) {
		return ENOMEM;
	}

	new->as_vbase1 = old->as_vbase1;
	new->as_npages1 = old->as_npages1;
	new->as_vbase2 = old->as_vbase2;
	new->as_npages2 = old->as_npages2;
	new->as_stackptr = old->as_stackptr;
	new->as_heaptop = old->as_heaptop;
	new->as_heapsz = old->as_heapsz;

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
