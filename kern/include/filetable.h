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

#ifndef _FILETABLE_H_
#define _FILETABLE_H_

/*
 * Definition of a file description entry
 *
 */
#include <types.h>
#include <vnode.h>
#include <lib.h>
#include <vfs.h>

/*
 * file descriptor table structures
 */
struct file_entry {
	struct vnode *vn; // vnode
	int openflags; // openflags for the file
	off_t seek; // seek position
	int f_refcount; // reference count tracking number of file descriptors
			// pointing to the file table entry
	struct lock *f_lock; 
};

struct filetable {
	int filetable_size; // size of the file table
	int last_fd; // last used file dsecriptor
	struct file_entry **entries; // dynamic array of file table entries
};

/*
 * Prototypes for file descriptor table functions
 */
struct file_entry *file_entry_create(void);

void file_entry_destroy(struct file_entry *file_entry);

struct filetable *filetable_create(void);

void filetable_destroy(struct filetable *filetable);

int filetable_grow(struct filetable *filetable);

int filetable_shrink(struct filetable *filetable);

struct filetable *filetable_copy(struct filetable *filetable);

#endif
