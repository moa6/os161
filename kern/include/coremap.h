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

struct coremap_entry {
	struct addrspace *ce_addrspace;
	bool ce_allocated;
	bool ce_foruser;
	bool ce_busy;
	int ce_next;
	int *ce_pgentry;
	int ce_swapoffset;
};

struct coremap {
	struct coremap_entry *c_entries;
	struct spinlock c_spinlock;
	unsigned long c_npages;
	bool c_ready;
};

extern struct coremap *coremap;

void coremap_entry_init(struct coremap_entry *coremap_entry);

void coremap_bootstrap(void);

bool coremap_ready(void);

paddr_t coremap_getkpages(unsigned long npages);

int coremap_getpage(int *pg_entry, struct addrspace *as);

void coremap_freekpages(paddr_t pframe);

void coremap_freepage(paddr_t pframe); 

#endif
