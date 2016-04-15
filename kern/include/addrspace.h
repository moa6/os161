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

#ifndef _ADDRSPACE_H_
#define _ADDRSPACE_H_

/*
 * Address space structure and operations.
 */


#include <vm.h>
#include "opt-dumbvm.h"

/* Define the maximum number of heap pages allowed per process */
#define MAX_HEAPPAGES 1260

/* Define the size of the stack page table.  The size of the stack page table
 * is static so the stack size per process is also limited */
#define NSTACKPAGES 20

/* Define the minimum size of the heap page table */
#define MIN_HEAPSZ 4

/* Define the types of address regions supported */
#define AS_REGION1 0
#define AS_REGION2 1
#define AS_HEAP 2
#define AS_STACK 3

/* Bitmasks for the page table entries */
#define PG_VALID 0x80000000
#define PG_FRAME 0x000FFFFF
#define PG_SWAP 0x40000000
#define PG_BUSY 0x20000000
#define PG_DIRTY 0x10000000

struct vnode;


/*
 * Address space - data structure associated with the virtual memory
 * space of a process.
 *
 * You write this.
 */

struct addrspace {
#if OPT_DUMBVM
        vaddr_t as_vbase1;
        paddr_t as_pbase1;
        size_t as_npages1;
        vaddr_t as_vbase2;
        paddr_t as_pbase2;
        size_t as_npages2;
        paddr_t as_stackpbase;
#else
        /* Address space lock */
	struct lock *as_lock;

	/* Address space cv */
	struct cv *as_cv;

	/* Pointer to the page table for address region 1 */
        int* as_pgtable1;

	/* Virtual base address of address region 1 */
	vaddr_t as_vbase1;

	/* Number of pages in address region 1 */
	unsigned long as_npages1;

	/* Pointer to the page table for address region 2 */
	int* as_pgtable2;

	/* Virtual base address of address region 2 */
	vaddr_t as_vbase2;

	/* Number of pages in address region 2 */
	unsigned long as_npages2;

	/* Pointer to the stack page table */
	int* as_stackpgtable;

	/* Address stack pointer */
	vaddr_t as_stackptr;

	/* Pointer to the heap page table */
	int* as_heappgtable;

	/* End address of the heap region */
	vaddr_t as_heaptop;

	/* Size of the heap page table */
	int as_heapsz;
#endif
};

/*
 * Functions in addrspace.c:
 *
 *    as_create - create a new empty address space. You need to make
 *                sure this gets called in all the right places. You
 *                may find you want to change the argument list. May
 *                return NULL on out-of-memory error.
 *
 *    as_growheap - Doubles the size of the heap page table.
 *
 *    as_shrinkheap - Reduces the size of the heap page table by half. 
 *
 *    as_copyregion - Copies the page table for an address region.  Invokes by
 *                    as_copy.
 *
 *    as_copy   - create a new address space that is an exact copy of
 *                an old one. Probably calls as_create to get a new
 *                empty address space and fill it in, but that's up to
 *                you.
 *
 *    as_activate - make curproc's address space the one currently
 *                "seen" by the processor.
 *
 *    as_deactivate - unload curproc's address space so it isn't
 *                currently "seen" by the processor. This is used to
 *                avoid potentially "seeing" it while it's being
 *                destroyed.
 *   
 *    as_destroyregion - destroys the page table of a given address region.
 *                       Invoked by as_destroy.
 *
 *    as_destroy - dispose of an address space. You may need to change
 *                the way this works if implementing user-level threads.
 *
 *    as_define_region - set up a region of memory within the address
 *                space.
 *
 *    as_prepare_load - this is called before actually loading from an
 *                executable into the address space.
 *
 *    as_complete_load - this is called when loading from an executable
 *                is complete.
 *
 *    as_define_stack - set up the stack region in the address space.
 *                (Normally called *after* as_complete_load().) Hands
 *                back the initial stack pointer for the new process.
 *
 * Note that when using dumbvm, addrspace.c is not used and these
 * functions are found in dumbvm.c.
 */

struct addrspace *as_create(void);
int		  as_growheap(struct addrspace *as);
int		  as_shrinkheap(struct addrspace *as);
int		  as_copyregion(struct addrspace *old, struct addrspace *new, int as_regiontype);
int               as_copy(struct addrspace *src, struct addrspace **ret);
void              as_activate(void);
void              as_deactivate(void);
void		  as_destroyregion(struct addrspace *as, int as_regiontype);
void              as_destroy(struct addrspace *);

int               as_define_region(struct addrspace *as,
                                   vaddr_t vaddr, size_t sz,
                                   int readable,
                                   int writeable,
                                   int executable);
int               as_prepare_load(struct addrspace *as);
int               as_complete_load(struct addrspace *as);
int               as_define_stack(struct addrspace *as, vaddr_t *initstackptr);


/*
 * Functions in loadelf.c
 *    load_elf - load an ELF user program executable into the current
 *               address space. Returns the entry point (initial PC)
 *               in the space pointed to by ENTRYPOINT.
 */

int load_elf(struct vnode *v, vaddr_t *entrypoint);


#endif /* _ADDRSPACE_H_ */
