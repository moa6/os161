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
#include <kern/errno.h>
#include <coremap.h>

struct coremap *coremap;

void
coremap_entry_init(struct coremap_entry *coremap_entry) {
	KASSERT(coremap_entry != NULL);

	coremap_entry->vaddr = 0;
	coremap_entry->next = 0;
	coremap_entry->allocated = false;
}

void
coremap_bootstrap(void) {
	paddr_t firstpaddr;
	paddr_t lastpaddr;
	unsigned long total_ppages;

	lastpaddr = ram_getsize();
	KASSERT((lastpaddr & PAGE_FRAME) == lastpaddr);
	total_ppages = (unsigned long)(lastpaddr / PAGE_SIZE);

	coremap = kmalloc(sizeof(struct coremap));
	if (coremap == NULL) {
		panic("kmalloc failed in coremap_boostrap\n");
	}

	coremap->ready = false;

	spinlock_init(&coremap->c_lock);

	coremap->c_entries = (struct coremap_entry*)kmalloc(total_ppages * sizeof(struct
	coremap_entry));
	if (coremap->c_entries == NULL) {
		panic("kmalloc failed in coremap_bootstrap\n");
	}

	for (unsigned long i=0; i<total_ppages; i++) {
		coremap_entry_init(&coremap->c_entries[i]);
	}

	firstpaddr = ram_getfirstfree();
	KASSERT((firstpaddr & PAGE_FRAME) == firstpaddr);

	for (unsigned long i=0; i<(unsigned long)(firstpaddr/PAGE_SIZE); i++) {
		coremap->c_entries[i].allocated = true;
	}

	coremap->total_ppages = total_ppages;
	coremap->firstpaddr = firstpaddr;
	coremap->ready = true;
}

bool
coremap_ready(void) {
	if (coremap == NULL) {
		return false;
	} else {
		return coremap->ready;
	}
}

paddr_t
coremap_getppages(unsigned long npages) {	

	paddr_t pframe;
	unsigned long total_ppages;
	unsigned long start;
	unsigned long end;

	spinlock_acquire(&coremap->c_lock);
	start = (unsigned long)(coremap->firstpaddr/PAGE_SIZE);
	total_ppages = coremap->total_ppages;
	
	while (start < total_ppages) {
		if (!coremap->c_entries[start].allocated) {
			end = start+npages-1;
			if (end >= total_ppages) {
				break;
			}

			while (end > start) {
				if (coremap->c_entries[end].allocated) {
					break;
				} else {
					end--;
				}
			}
		
			if (start == end) {
				for (unsigned i=start; i<start+npages; i++) {
					coremap->c_entries[i].allocated = true;

					if (i < start+npages-1) {
						coremap->c_entries[i].next = 1;
					} else {
						coremap->c_entries[i].next = 0;
					}				
				}

				pframe = (paddr_t)(start*PAGE_SIZE);
				spinlock_release(&coremap->c_lock);
				return pframe;
			} 
					
		}

		start++;
	}

	spinlock_release(&coremap->c_lock);
	return 0;
}

void
coremap_freeppages(paddr_t pframe) {
	unsigned long index;

	spinlock_acquire(&coremap->c_lock);
	index = (unsigned long)(pframe / PAGE_SIZE);
	
	while (coremap->c_entries[index].next != 0) {
		coremap->c_entries[index].next = 0;
		KASSERT(coremap->c_entries[index].allocated == true);
		coremap->c_entries[index].allocated = false;
		index++;
	}

	KASSERT(coremap->c_entries[index].allocated == true);
	coremap->c_entries[index].allocated = false;
	spinlock_release(&coremap->c_lock);
}
