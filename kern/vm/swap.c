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
#include <thread.h>
#include <synch.h>
#include <bitmap.h>
#include <vfs.h>
#include <vm.h>
#include <uio.h>
#include <vnode.h>
#include <coremap.h>
#include <addrspace.h>
#include <kern/fcntl.h>
#include <kern/errno.h>
#include <lamebus/lhd.h>
#include <swap.h>

struct swap *kswap;

void
sw_lruclk_create(void) {
	unsigned nbits;

	kswap->sw_lruclk = kmalloc(sizeof(struct lru_clk));
	if (kswap->sw_lruclk == NULL) {
		panic("kmalloc in sw_lruclk_create failed\n");
	}

	nbits = (unsigned)(ram_getsize()/PAGE_SIZE);
	kswap->sw_lruclk->l_clkface = bitmap_create(nbits);
	if (kswap->sw_lruclk->l_clkface == NULL) {
		panic("bitmap_create in sw_lruclk_create failed\n");
	}

	kswap->sw_lruclk->l_npages = (unsigned long)nbits;

	kswap->sw_lruclk->l_lock = lock_create("lru clk lock");
	if (kswap->sw_lruclk->l_lock == NULL) {
		panic("lock_create in sw_lruclk_create failed\n");
	}
} 

void
sw_bootstrap(void) {

	int result;

	kswap = kmalloc(sizeof(struct swap));
	if (kswap == NULL) {
		panic("kmalloc in sw_bootstrap failed\n");
	}

	sw_lruclk_create();

	kswap->sw_diskoffset = bitmap_create((unsigned)SWAP_SIZE);
	if (kswap->sw_diskoffset == NULL) {
		panic("bitmap_create in sw_bootstrap failed\n");
	}

	char swap_file_name[]="lhd0raw:";
	result = vfs_open(swap_file_name, O_RDWR, 0, &kswap->sw_file);
	if (result) {
		panic("vfs_open in sw_bootstrap failed\n");
	}

//	swap_test();

	kswap->sw_ts = kmalloc(sizeof(const struct tlbshootdown));
	if (kswap->sw_ts == NULL) {
		panic("kmalloc in sw_bootstrap failed\n");
	}

	kswap->sw_pglock = lock_create("swap lock");
	if (kswap->sw_pglock == NULL) {
		panic("lock_create in sw_bootstrap failed\n");
	}

	kswap->sw_disklock = lock_create("swap disk lock");
	if (kswap->sw_disklock == NULL) {
		panic("lock_create in sw_bootstrap failed\n");
	}

	kswap->sw_diskio_lock = lock_create("swap disk io lock");
	if (kswap->sw_diskio_lock == NULL) {
		panic("lock_create in sw_bootstrap failed\n");
	}

	kswap->sw_cv = cv_create("swap cv");
	if (kswap->sw_cv == NULL) {
		panic("cv_create in sw_bootstrap failed\n");
	}

	kswap->sw_pgavail = false;
	kswap->sw_pgvictim = 0;
/*
	result = thread_fork("LRU clock thread", NULL, sw_mv_clkhand, NULL, 0);
	if (result) {
		panic("thread_fork in sw_bootstrap failed\n");
	}
*/
}

void
sw_mv_clkhand(void *p, unsigned long arg) {
	(void)p;
	(void)arg;

	while (1) {
		for (unsigned hand=0; hand<(unsigned)kswap->sw_lruclk->l_npages; hand++) {
			lock_acquire(kswap->sw_lruclk->l_lock);

			if (bitmap_isset(kswap->sw_lruclk->l_clkface, hand)) {
				bitmap_unmark(kswap->sw_lruclk->l_clkface, hand);

			} else {
				lock_acquire(kswap->sw_pglock);
				kswap->sw_pgavail = true;
				kswap->sw_pgvictim = (paddr_t)(hand*PAGE_SIZE);
				cv_signal(kswap->sw_cv, kswap->sw_pglock);
				lock_release(kswap->sw_pglock);
				
			}

			lock_release(kswap->sw_lruclk->l_lock);
		}
	}
}

void
sw_getpage(paddr_t *paddr) {
	KASSERT(spinlock_do_i_hold(&coremap->c_spinlock));

	int index;

	while(1) {
/*
		lock_acquire(kswap->sw_pglock);

		while (!kswap->sw_pgavail) {
			spinlock_release(&coremap->c_spinlock);
			cv_wait(kswap->sw_cv, kswap->sw_pglock);
			spinlock_acquire(&coremap->c_spinlock);
		}
		
		*paddr = kswap->sw_pgvictim;
		kswap->sw_pgavail = false;
		lock_release(kswap->sw_pglock);
		index = (int)(*paddr/PAGE_SIZE);
*/

		index = random() % coremap->c_npages;
		*paddr = (paddr_t)(index*PAGE_SIZE);

		if (!coremap->c_entries[index].ce_allocated ||
		!coremap->c_entries[index].ce_foruser ||
		coremap->c_entries[index].ce_busy) {
			continue;

		} else {
			KASSERT(sw_check(coremap->c_entries[index].ce_pgentry,
			coremap->c_entries[index].ce_addrspace));

			lock_acquire(coremap->c_entries[index].ce_addrspace->as_lock);
			
			if (*coremap->c_entries[index].ce_pgentry & PG_BUSY ||
			!(*coremap->c_entries[index].ce_pgentry & PG_VALID)) {
				lock_release(coremap->c_entries[index].ce_addrspace->as_lock);
				continue;
			}

			*coremap->c_entries[index].ce_pgentry |= PG_BUSY;
			lock_release(coremap->c_entries[index].ce_addrspace->as_lock);
			break;
		}
	}
}

int
sw_evictpage(paddr_t paddr) {
	KASSERT(paddr != 0);

	struct lock *ce_aslock;
	struct cv *ce_ascv;
	struct uio ku;
	struct iovec iov;
	int index;
	int result;
	unsigned sw_offset;

	KASSERT(spinlock_do_i_hold(&coremap->c_spinlock));

	index = (int)(paddr/PAGE_SIZE);

	KASSERT(coremap->c_entries[index].ce_busy);
	KASSERT(coremap->c_entries[index].ce_foruser);
	KASSERT(!(*coremap->c_entries[index].ce_pgentry & PG_SWAP));
	KASSERT(*coremap->c_entries[index].ce_pgentry & PG_BUSY);
	KASSERT(*coremap->c_entries[index].ce_pgentry & PG_VALID);
	KASSERT((paddr_t)((*coremap->c_entries[index].ce_pgentry & PG_FRAME) <<
	12) == paddr);

	spinlock_release(&coremap->c_spinlock);
	ce_aslock = coremap->c_entries[index].ce_addrspace->as_lock;
	ce_ascv = coremap->c_entries[index].ce_addrspace->as_cv;

	if (*coremap->c_entries[index].ce_pgentry & PG_DIRTY) {
	
		lock_acquire(kswap->sw_disklock);

		result = bitmap_alloc(kswap->sw_diskoffset, &sw_offset);
		if (result) {
			lock_release(kswap->sw_disklock);
			spinlock_acquire(&coremap->c_spinlock);
			return ENOMEM;
		}

		lock_release(kswap->sw_disklock);

	}

	kswap->sw_ts->ts_paddr = paddr;
	vm_tlbshootdown(kswap->sw_ts);

	if (*coremap->c_entries[index].ce_pgentry & PG_DIRTY) {
//		lock_acquire(kswap->sw_diskio_lock);
		uio_kinit(&iov, &ku, (void *)PADDR_TO_KVADDR(paddr), PAGE_SIZE,
		(off_t)(sw_offset*PAGE_SIZE), UIO_WRITE);
		result = VOP_WRITE(kswap->sw_file, &ku);
//		lock_release(kswap->sw_diskio_lock);
		if (result) {
			*coremap->c_entries[index].ce_pgentry &= ~PG_BUSY;
			cv_signal(ce_ascv, ce_aslock);
			spinlock_acquire(&coremap->c_spinlock);
			return result;
		}	
		
		*coremap->c_entries[index].ce_pgentry = PG_SWAP | (int)sw_offset;

	} else {
		*coremap->c_entries[index].ce_pgentry = PG_SWAP |
		coremap->c_entries[index].ce_swapoffset;

	}

	cv_signal(ce_ascv, ce_aslock);
	spinlock_acquire(&coremap->c_spinlock);
	return 0;
}

int
sw_pagein(int *pg_entry, struct addrspace *as) {
	paddr_t paddr;
	off_t sw_offset;
	int result;
	int saved_entry;
	struct uio ku;
	struct iovec iov;

	KASSERT(sw_check(pg_entry, as));

	sw_offset = (off_t)((*pg_entry & PG_FRAME) * PAGE_SIZE);
	saved_entry = *pg_entry;

	result = coremap_getpage(pg_entry, as);
	if (result) {
		*pg_entry = saved_entry;
		return result;
	}
	
	paddr = (*pg_entry & PG_FRAME) << 12;
	uio_kinit(&iov, &ku, (void *)PADDR_TO_KVADDR(paddr), PAGE_SIZE,
	sw_offset, UIO_READ);

	result = VOP_READ(kswap->sw_file, &ku);
	if (result) {
		*pg_entry = saved_entry;
		return result;
	}

	lock_acquire(kswap->sw_disklock);
	bitmap_unmark(kswap->sw_diskoffset, (unsigned)(sw_offset/PAGE_SIZE));
	lock_release(kswap->sw_disklock);

	*pg_entry &= ~PG_SWAP;
	*pg_entry |= PG_VALID;
	*pg_entry |= PG_DIRTY;

	return 0;
}

void
swap_test(void) {
	struct uio ku;
	struct iovec iov;

	int result;
	int write[LHD_SECTSIZE];
	int read[LHD_SECTSIZE];

	for (int i=0; i<LHD_SECTSIZE; i++) {
		write[i] = i;
	}

	uio_kinit(&iov, &ku, write, sizeof(write),
	0, UIO_WRITE);
	result = VOP_WRITE(kswap->sw_file, &ku);
	if (result) {
		panic("Write failed in swap_test\n");
	}

	uio_kinit(&iov, &ku, read, sizeof(read),
	0, UIO_READ);
	result = VOP_READ(kswap->sw_file, &ku);
	if (result) {
		panic("Read failed in swap_test\n");
	}

	for (int i=0; i<LHD_SECTSIZE; i++) {
		if (read[i] != write[i]) {
			panic("Mismatch in swap_test\n");
		}
	}
}

bool
sw_check(int *pg_entry, struct addrspace *as) {
	unsigned long as_npages1;
	unsigned long as_npages2;
	int as_stacksz;
	int as_heapsz;

	as_npages1 = as->as_npages1;
	as_npages2 = as->as_npages2;
	as_stacksz = as->as_stacksz;
	as_heapsz = as->as_heapsz;

	for (unsigned long i=0; i<as_npages1; i++) {
		if (&as->as_pgtable1[i] == pg_entry) {
			return true;
		}
	}

	for (unsigned long i=0; i<as_npages2; i++) {
		if (&as->as_pgtable2[i] == pg_entry) {
			return true;
		}
	}

	for (int i=0; i<as_heapsz; i++) {
		if (&as->as_heappgtable[i] == pg_entry) {
			return true;
		}
	}

	for (int i=0; i<as_stacksz; i++) {
		if (&as->as_stackpgtable[i] == pg_entry) {
			return true;
		}
	}

	return false;

}
