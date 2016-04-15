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
#include <cpu.h>
#include <clock.h>
#include <spinlock.h>
#include <synch.h>
#include <bitmap.h>
#include <vfs.h>
#include <vm.h>
#include <uio.h>
#include <vnode.h>
#include <coremap.h>
#include <addrspace.h>
#include <thread.h>
#include <kern/fcntl.h>
#include <kern/errno.h>
#include <lamebus/lhd.h>
#include <swap.h>

struct swap *kswap;

/*
 * sw_bootstrap
 *
 * Initializes the kernel swap structure in boot up
 */
void
sw_bootstrap(void) {

	int result;

	/* Allocate space for the kernel swap structure */
	kswap = kmalloc(sizeof(struct swap));
	if (kswap == NULL) {
		panic("kmalloc in sw_bootstrap failed\n");
	}

	/* Create the bitmap for tracking the offset locations available in the
	 * swap file */
	kswap->sw_diskoffset = bitmap_create((unsigned)SWAPFILE_SIZE);
	if (kswap->sw_diskoffset == NULL) {
		panic("bitmap_create in sw_bootstrap failed\n");
	}

	/* Create a swap file */
	char swap_file_name[]="lhd0raw:";
	result = vfs_open(swap_file_name, O_RDWR, 0, &kswap->sw_vn);
	if (result) {
		panic("vfs_open in sw_bootstrap failed\n");
	}

	/* Create the tlbshootdown structure */
	kswap->sw_ts = kmalloc(sizeof(const struct tlbshootdown));
	if (kswap->sw_ts == NULL) {
		panic("kmalloc in sw_bootstrap failed\n");
	}

	/* Create the lock which ensures updates to the offset locations in the
	 * swap file are made atomically */
	kswap->sw_disklock = lock_create("swap disk lock");
	if (kswap->sw_disklock == NULL) {
		panic("lock_create in sw_bootstrap failed\n");
	}

	/* Create the lock which protects sw_ts */
	kswap->sw_tlbshootdown_lock = lock_create("swap tlbshootdown lock");
	if (kswap->sw_tlbshootdown_lock == NULL) {
		panic("lock_create in sw_bootstrap failed\n");
	}

	/* Create the semaphore which is used to communicate when tlbshootdowns
	 * are complete */
	kswap->sw_tlbshootdown_sem = sem_create("sw tlbshootdown sem", 0);
	if (kswap->sw_tlbshootdown_sem == NULL) {
		panic("sem_create in sw_bootstrap failed\n");
	}

	kswap->sw_pgevicted = false;

	/* Create the page daemon... except that the page daemon is not working
	 * right now so we comment it out.  :) */
//	thread_fork("Page eviction thread", NULL, evicting, NULL, 0);

}

/*
 * sw_getpage
 *
 * Picks a page for eviction
 */
void
sw_getpage(paddr_t *paddr) {
	int c_index;
	paddr_t evicted_paddr;

	while(1) {
		/* Acquire the coremap spinlock */
		spinlock_acquire(&coremap->c_spinlock);
	
		/* For simplicity sake, we randomly select a page which isn't
		 * reserved for the kernel to be evicted */
		c_index = coremap->c_userpbase + (int)(random() % (coremap->c_npages -
		coremap->c_userpbase));

		/* If the coremap entry indicates that the physical page is
		 * free, or that it is already being evicted, or that it is
		 * resreved for the kernel, then the page we selected cannot be
		 * evicted.  We must try again later. */
		if (!coremap->c_entries[c_index].ce_allocated ||
		coremap->c_entries[c_index].ce_busy ||
		!coremap->c_entries[c_index].ce_foruser) {
			spinlock_release(&coremap->c_spinlock);
			thread_yield();
			continue;

		} else {

			/* Mark the coremap entry as being busy */
			coremap->c_entries[c_index].ce_busy = true;
			
			/* Release the coremap spinlock */
			spinlock_release(&coremap->c_spinlock);

			/* Acquire the lock in the address space which contains
			 * the page table entry pointing to the physical page */
			lock_acquire(coremap->c_entries[c_index].ce_addrspace->as_lock);

			if (*coremap->c_entries[c_index].ce_pgentry & PG_BUSY ||
			*coremap->c_entries[c_index].ce_pgentry &
			PG_SWAP) {

				/* If the page table entry is marked as busy or
				 * as swapped, we cannot evict the page.  We
				 * release the address lock and mark the coremap
				 * entry as not being busy. */

				lock_release(coremap->c_entries[c_index].ce_addrspace->as_lock);
				spinlock_acquire(&coremap->c_spinlock);
				coremap->c_entries[c_index].ce_busy = false;
				spinlock_release(&coremap->c_spinlock);
				thread_yield();
				continue;

			} else if (*coremap->c_entries[c_index].ce_pgentry &
			PG_VALID) {

				/* The page table entry pointing to the page is
				 * marked as valid.  So we can evict this page.
				 * We mark the page table entry as being busy so
				 * that when a vm_fault occurs on this physical
				 * address, the data in the physical page
				 * will not be modified until the page eviction
				 * is complete.
				 */

				*coremap->c_entries[c_index].ce_pgentry |=
				PG_BUSY;
				evicted_paddr =
				(paddr_t)((*coremap->c_entries[c_index].ce_pgentry
				& PG_FRAME) << 12);
				KASSERT(evicted_paddr == (paddr_t)(c_index*PAGE_SIZE));
	
				/* Execute the tlbshootdown */
				execute_tlbshootdown(evicted_paddr);

				/* Release the address space lock */
				lock_release(coremap->c_entries[c_index].ce_addrspace->as_lock);

				/* Set the value of paddr to the page we wish to
				 * evict */
				*paddr = evicted_paddr;

				break;

			} else {
				panic("sw_getpage should not get here\n");
			}
		}			
	}


}

/*
 * sw_evictpage
 *
 * Performs the actual page eviction
 */
int
sw_evictpage(paddr_t paddr) {

	struct iovec iov;
	struct uio ku;
	int c_index;
	int result;
	unsigned sw_offset;

	c_index = (int)(paddr/PAGE_SIZE);
	
	if (*coremap->c_entries[c_index].ce_pgentry & PG_DIRTY) {

		/* If the page is dirty, we need to write its contents to disk.
		 * In our current implementation, all pages are assumed to be
		 * dirty so sw_evictpage should always execute this branch of
		 * the code. */

		/* Determine if there is space in the swap file to write the
		 * page contents to disk */
		lock_acquire(kswap->sw_disklock);
		result = bitmap_alloc(kswap->sw_diskoffset, &sw_offset);
		lock_release(kswap->sw_disklock);
		if (result) {

			/* If the disk is full, we mark the coremap entry as not
			 * being busy and we wake up any thread waiting to
			 * access the page.  We also mark the disk as being
			 * full. */

			kswap->sw_diskfull = true;
			lock_acquire(coremap->c_entries[c_index].ce_addrspace->as_lock);
			*coremap->c_entries[c_index].ce_pgentry &= ~PG_BUSY;
			cv_signal(coremap->c_entries[c_index].ce_addrspace->as_cv,
			coremap->c_entries[c_index].ce_addrspace->as_lock);
			lock_release(coremap->c_entries[c_index].ce_addrspace->as_lock);
			return ENOMEM;
		}

		kswap->sw_diskfull = false;

		/* Write the contents of the page to the swap file */
		uio_kinit(&iov, &ku, (void *)PADDR_TO_KVADDR(paddr), PAGE_SIZE,
		(off_t)(sw_offset*PAGE_SIZE), UIO_WRITE);
		result = kswap->sw_vn->vn_ops->vop_write(kswap->sw_vn, &ku);
		if (result) {

			/* If we were unable to write the contents of the page
			 * to disk, we mark the coremap entry as not being busy
			 * and wake up any thread wishing to access the data in
			 * the page. */

			lock_acquire(coremap->c_entries[c_index].ce_addrspace->as_lock);
			*coremap->c_entries[c_index].ce_pgentry &= ~PG_BUSY;
			cv_signal(coremap->c_entries[c_index].ce_addrspace->as_cv,
			coremap->c_entries[c_index].ce_addrspace->as_lock);
			lock_release(coremap->c_entries[c_index].ce_addrspace->as_lock);
			return result;
		}

		/* We have successfully evicted the page.  We set the swap flag
		 * in the page table entry.  In place of the page frame in the
		 * page table entry, we place the offset in the swap file where
		 * the page table contents are stored.*/
		lock_acquire(coremap->c_entries[c_index].ce_addrspace->as_lock);
		*coremap->c_entries[c_index].ce_pgentry = PG_SWAP |
		(int)(sw_offset);

		/* We wake up any threads which have been waiting for the page
		 * eviction to be completed */
		cv_signal(coremap->c_entries[c_index].ce_addrspace->as_cv,
		coremap->c_entries[c_index].ce_addrspace->as_lock);
		lock_release(coremap->c_entries[c_index].ce_addrspace->as_lock);

	} else {

		panic("All pages are assumed dirty for now...\n");

	} 

	return 0;
}

/*
 * sw_pagein
 *
 * Performs a page in if the page table entry indicates that the contents of the
 * page are on disk
 */
int
sw_pagein(int *pg_entry, struct addrspace *as) {
	paddr_t paddr;
	off_t sw_offset;
	int result;
	int saved_entry;
	struct uio ku;
	struct iovec iov;

	/* Obtain the location in the swap file where the page contents are
	 * stored */
	sw_offset = (off_t)((*pg_entry & PG_FRAME) * PAGE_SIZE);
	saved_entry = *pg_entry;

	/* Obtain a new page */
	result = coremap_getpage(pg_entry, as);
	if (result) {
		*pg_entry = saved_entry;
		return result;
	}
	
	paddr = (*pg_entry & PG_FRAME) << 12;

	/* Read the page contents from the swap file into the new page */
	uio_kinit(&iov, &ku, (void *)PADDR_TO_KVADDR(paddr), PAGE_SIZE,
	sw_offset, UIO_READ);
	result = kswap->sw_vn->vn_ops->vop_read(kswap->sw_vn, &ku);
	if (result) {
		*pg_entry = saved_entry;
		return result;
	}

	/* Mark the offset location in the swap file as being free, now that the
	 * data is in memory */
	lock_acquire(kswap->sw_disklock);
	bitmap_unmark(kswap->sw_diskoffset, (unsigned)(sw_offset/PAGE_SIZE));
	lock_release(kswap->sw_disklock);

	/* Unset the swap flag in the page table entry */
	*pg_entry &= ~PG_SWAP;

	/* Mark the page table entry as being valid and dirty */
	*pg_entry |= PG_VALID;
	*pg_entry |= PG_DIRTY;

	return 0;
}

/*
 * evicting
 *
 * Code which the page daemon runs
 */
void
evicting(void *p, unsigned long arg) {

	(void)p;
	(void)arg;
	paddr_t pgvictim;
	int result;
	int c_index;

	while(1) {

		/* Set kswap->sw_pgevicted as false to indicate the page daemon
		 * has not yet evicted a page */
		kswap->sw_pgevicted = false;

		/* Select a page to evict */
		sw_getpage(&pgvictim);

		/* Check that the coremap entry corresponding to the page
		 * selected for eviction has the appropriate values */
		KASSERT(!spinlock_do_i_hold(&coremap->c_spinlock));
		c_index = (int)(pgvictim/PAGE_SIZE);
		KASSERT(coremap->c_entries[c_index].ce_allocated);
		KASSERT(coremap->c_entries[c_index].ce_foruser);
		KASSERT(coremap->c_entries[c_index].ce_busy);
		KASSERT((paddr_t)((*coremap->c_entries[c_index].ce_pgentry &
		PG_FRAME) << 12) == pgvictim);

		/* Evict the page */
		result = sw_evictpage(pgvictim);
		if (result) {
			spinlock_acquire(&coremap->c_spinlock);
			coremap->c_entries[c_index].ce_busy = false;
			spinlock_release(&coremap->c_spinlock);
			clocksleep(1);
		}

		/* Mark the coremap entry as being free */
		spinlock_acquire(&coremap->c_spinlock);
		coremap->c_entries[c_index].ce_allocated = false;
		coremap->c_entries[c_index].ce_addrspace = NULL;

		if (c_index >= coremap->c_userpbase) {
			coremap->c_entries[c_index].ce_foruser = true;

		} else {
			coremap->c_entries[c_index].ce_foruser = false;
		}

		coremap->c_entries[c_index].ce_next = 0;
		coremap->c_entries[c_index].ce_pgentry = NULL;
		coremap->c_entries[c_index].ce_swapoffset = -1;
		coremap->c_entries[c_index].ce_busy = false;
		spinlock_release(&coremap->c_spinlock);
		kswap->sw_pgevicted = true;
		clocksleep(1);

	}

}
