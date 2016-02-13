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
#include <vnode.h>
#include <lib.h>
#include <filetable.h>
#include <kern/fcntl.h>

/*
 * file_entry_create
 *
 * file_entry_create creates the file table entry.
 */
struct file_entry *
file_entry_create(void) {
	/* Allocate a file table entry struct */
	struct file_entry *file_entry;
	file_entry = kmalloc(sizeof(*file_entry));
	
	if(file_entry == NULL) {
		return NULL;
	}

	/* Initialize the fields in the file table entry */
	file_entry->vn = NULL;
	file_entry->openflags = 0;
	file_entry->f_refcount = 0;
	return file_entry;
}

/*
 * file_entry_destroy
 *
 * file_entry_destroy destroys the file table entry
 */
void
file_entry_destroy(struct file_entry *file_entry) {
	KASSERT(file_entry != NULL);
	kfree(file_entry);
}

/*
 * filetable_create
 *
 * filetable_create creates the file table.
 */
struct filetable*
filetable_create(void) {
	int i;
	int result;
	int filetable_size;
	struct filetable *filetable;
	struct vnode *vn;
	struct file_entry **temp;
	char *con_stdin;
	char *con_stdout;
	char *con_stderr;

	/* Allocate a file table and set it to an initial size of 8 */
	filetable = kmalloc(sizeof(*filetable));
	filetable_size = 8;
	filetable->filetable_size = filetable_size;
	temp = kmalloc(filetable_size*sizeof(*filetable->entries));
	if (temp == NULL) {
		KASSERT(temp != NULL);
		return NULL;
	}
	filetable->entries = temp;	

/*
	if (filetable->entries == NULL) {
		KASSERT(filetable->entries != NULL);		
		return NULL;
	}
*/

	/* Reserve file descriptors 0, 1, and 2 for STDIN, STDOUT, and STDERR
 * respectively */
	for (i=0; i<3; i++) {
		filetable->entries[i] = file_entry_create();

		if (filetable->entries[i] == NULL) {
			panic("filetable: entry_create failed\n");
		}
	}
	char con[]="con:";
	con_stdin = kmalloc(sizeof(con));
	con_stdout = kmalloc(sizeof(con));
	con_stderr = kmalloc(sizeof(con));
	strcpy(con_stdin, con);
	strcpy(con_stdout, con);
	strcpy(con_stderr, con);
		
	result = vfs_open(con_stdin, O_RDONLY, 0664, &vn);
	if (result) {
		kfree(con_stdin);
		kfree(con_stdout);
		kfree(con_stderr);
		kfree(temp);
		return NULL;
	}
	filetable->entries[0]->vn = vn;
	filetable->entries[0]->openflags = O_RDONLY;
	filetable->entries[0]->seek = 0;
	filetable->entries[0]->f_refcount++;

	result = vfs_open(con_stdout, O_WRONLY, 0664, &vn);
	if (result) {
		kfree(con_stdin);
		kfree(con_stdout);
		kfree(con_stderr);
		kfree(temp);
		return NULL;
	}
	filetable->entries[1]->vn = vn;
	filetable->entries[1]->openflags = O_WRONLY;
	filetable->entries[1]->seek = 0;
	filetable->entries[1]->f_refcount++;

	result = vfs_open(con_stderr, O_WRONLY, 0664, &vn);
	if (result) {
		kfree(con_stdin);
		kfree(con_stdout);
		kfree(con_stderr);
		kfree(temp);
		return NULL;
	}
	filetable->entries[2]->vn = vn;
	filetable->entries[2]->openflags = O_WRONLY;
	filetable->entries[2]->seek = 0;
	filetable->entries[2]->f_refcount++;
	
	/* Free allocated memory */
	kfree(con_stdin);
	kfree(con_stdout);
	kfree(con_stderr);

	/* Set last_fd to 2 */
	filetable->last_fd = 2;
	/* Return the file table */
	return filetable;
}

/*
 * filetable_destroy
 *
 * filetable_destroy destroys the file table/
 */
void
filetable_destroy(struct filetable *filetable) {
	int i;

	KASSERT(filetable != NULL);
	for (i=0; i<filetable->filetable_size; i++) {
		file_entry_destroy(filetable->entries[i]);
	}
	kfree(filetable);
}

/*
 * filetable_grow
 *
 * filetable_grow doubles the size of the file table/
 */
void
filetable_grow(struct filetable *filetable) {
	int old_size;
	int new_size;
	struct file_entry **temp;

	/* Get the original size of the file table */
	old_size = filetable->filetable_size;
	/* Calculate the new size of the file table */
	new_size = 2*old_size;

	/* Re-size the file table */
	temp = kmalloc(new_size*sizeof(struct filetable_entry*));
	if (temp == NULL) {
		panic("filetable_grow: kmalloc failed\n");
	}
	memcpy(temp, filetable->entries, old_size*sizeof(struct filetable_entry*));
	kfree(filetable->entries);
	filetable->entries = temp;
	filetable->filetable_size = new_size;
}

/*
 * filetable_shrink
 *
 * filetable_shrink halves the size of the file table.
 */
void
filetable_shrink(struct filetable *filetable) {
	int old_size;
	int new_size;
	struct file_entry **temp;
	
	/* Get the original size of the file table */
	old_size = filetable->filetable_size;
	/* Calculate the new size of the file table */
	new_size = old_size/2;

	/* Re-size of the file table */
	temp = kmalloc(new_size*sizeof(struct filetable_entry*));
	if (temp == NULL) {
		panic("filetable_shrink: kmalloc failed\n");
	}
	memcpy(temp, filetable->entries, new_size*sizeof(struct filetable_entry*));
	kfree(filetable->entries);
	filetable->entries = temp;
	filetable->filetable_size = new_size;
}
