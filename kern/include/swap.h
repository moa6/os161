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

#ifndef _SWAP_H_
#define _SWAP_H_

#include <types.h>
#include <lib.h>

/* Define the size of the swap file */
#define SWAPFILE_SIZE 1250

/*
 * swap struct
 */
struct swap {
	/* tlbshootdown structure */
	struct tlbshootdown *sw_ts;

	/* Semaphore used to communicate when tlbshootdown is complete */
	struct semaphore *sw_tlbshootdown_sem;

	/* Lock which protects sw_ts */
	struct lock *sw_tlbshootdown_lock;

	/* Lock which ensures that offset locations in the swap file are updated
	 * atomically */
	struct lock *sw_disklock;

	/* Swap file vnode */
	struct vnode *sw_vn;

	/* bitmap which tracks the offset locations in the swap file */
	struct bitmap *sw_diskoffset;

	/* Flag to indicate if the disk is full */
	bool sw_diskfull;

	/* Flag to indicate if the page daemon thread has evicted the page */
	bool sw_pgevicted;
};

/* The kernel swap stucture */
extern struct swap *kswap;

/* Declarations of swap functions */

void sw_bootstrap(void);

void sw_getpage(paddr_t *paddr);

int sw_evictpage(paddr_t paddr);

int sw_pagein(int *pg_entry, struct addrspace *as);

void evicting (void *p, unsigned long arg);

#endif
