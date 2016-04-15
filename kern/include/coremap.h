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

#ifndef _COREMAP_H_
#define _COREMAP_H_

#include <types.h>
#include <lib.h>

/* Define the number of physical pages we reserve exclusively for the kernel.*/
#define N_RESERVEDKPAGES 30

/*
 * coremap_entry struct
 */
struct coremap_entry {

	/* If the physical page represented by the coremap entry is used by the
	 * kernel, ce_addrspace is NULL.  Otherwise, if it is used in a page
	 * table entry, ce_addrspace points to the address space containing that
	 * page table. */
	struct addrspace *ce_addrspace;

	/* ce_allocated indicates if the physical page is free or not */
	bool ce_allocated;

	/* ce_foruser indicates if the physical page is used by the kernel or by
	 * a user process. */
	bool ce_foruser;

	/* ce_busy is set if the physical page is in the process of being
	 * evicted. */
	bool ce_busy;

	/* ce_next is only meaningful if the physical page is used by the
	 * kernel.  The field ce_next is set to 1 if it is contiguous with
	 * another kernel page. */
	int ce_next;

	/* ce_pgentry is only meaningful if the physical page is used by a user
	 * process.  The field ce_pgentry points to the page table entry
	 * referencing it. */
	int *ce_pgentry;

	/* ce_swapoffset is used if the physical page has been cleaned.  Wit our
	 * current implementation, we assume all pages are dirty so this field
	 * is unused. */
	int ce_swapoffset;
};

/*
 * coremap struct
 */
struct coremap {

	/* c_entries is an array of coremap entries */
	struct coremap_entry *c_entries;

	/* c_spinlock is the spinlock protecting the coremap */
	struct spinlock c_spinlock;

	/* c_npages is the number of physical pages in the system */
	unsigned long c_npages;

	/* c_ready indicates if the kernel has finished initializing the
	 * coremap.  Until the coremap is ready, kmalloc uses ram_stelamem to
	 * allocate memory. */
	bool c_ready;

	/* c_kernelpbase is the first physical address which can be allocated
	 * and freed by the kernel.  The physical addresses prior to c_kernelpbase
	 * are for data structures created during bootup like the coremap.  It
	 * is probably not a good idea to be freeing those... */
	int c_kernelpbase;

	/* c_userpbase is the first physical address which can be used by the
	 * user process. The physical addresses prior to c_userpbase are used by
	 * the kernel.*/
	int c_userpbase;
};

/* Declarations of coremap functions */

extern struct coremap *coremap;

void coremap_entry_init(struct coremap_entry *coremap_entry);

void coremap_bootstrap(void);

bool coremap_ready(void);

paddr_t coremap_getkpages(unsigned long npages);

int coremap_getpage(int *pg_entry, struct addrspace *as);

void coremap_freekpages(paddr_t pframe);

#endif
