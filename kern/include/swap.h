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

#define SWAP_SIZE 1250

struct lru_clk {
	struct lock *l_lock;
	struct bitmap *l_clkface;
	unsigned long l_npages;
};

struct swap {
	struct tlbshootdown *sw_ts;
	paddr_t sw_pgvictim;
	bool sw_pgavail;
	struct lock *sw_pglock;
	struct lock *sw_disklock;
	struct lock *sw_diskio_lock;
	struct cv *sw_cv;
	struct vnode *sw_file;
	struct bitmap *sw_diskoffset;
	struct lru_clk *sw_lruclk;
};

extern struct swap *kswap;

void sw_lruclk_create(void);

void sw_bootstrap(void);

void sw_mv_clkhand(void *p, unsigned long arg);

void sw_getpage(paddr_t *paddr);

int sw_evictpage(paddr_t paddr);

int sw_pagein(int *pg_entry, struct addrspace *as);

void swap_test(void);

bool sw_check(int *pg_entry, struct addrspace *as);
#endif
