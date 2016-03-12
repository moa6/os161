/*
 * Copyright (c) 2009
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

#ifndef _PROCLIST_H_
#define _PROCLIST_H_

#include <types.h>
#include <lib.h>


/*
 * Procnode struct
 * The procnode structure is used by a child process to communicate its exit
 * status to its parent.  Each procnode is pointed to by both the parent process
 * and the child process.  
*/
struct procnode {
	pid_t pid; 			/* pid of the child process sharing this
procnode */
	int exitcode;			/* exitcode from the child process */

	unsigned pn_refcount;		/* reference count used to track how
					   many processes are pointing to the
					   procnode.  Only a maximum of  
					   two processes point to the procnode:
					   a parent process and a child process.*/

	struct lock *pn_lock;		/* Lock to ensure pn_refcount is updated
					   atomically */

	struct semaphore *waitsem;	/* Semaphore used to synchronize the
					   parent and child processes */
	
	struct procnode *previous;	/* pointer to previous procnode in a
					   procnode_list */

	struct procnode *next;		/* pointer to the next procnode in a
					   procnode_list */
};

/* procnode_list
 * A procnode_list is a list of procnodes.  Each process has a list of procnodes
 * which is updated whenever the process forks to create a new child.
 */
struct procnode_list {
	struct procnode *head;
};

/* Create and initialize a procnode */
struct procnode* procnode_create(void);

/* Create and initialize a procnode_list */
struct procnode_list* procnode_list_create(void);

/* Destroy a procnode */
void procnode_destroy(struct procnode* procnode);

/* Destroy a procnode_list */
void procnode_list_destroy(struct procnode_list* pn_list);

/* Checks if a procnode_list is empty of procnodes */
bool procnode_list_isempty(struct procnode_list* pn_list);

/* Finds a procnode with a particular PID in a procnode_list.  Returns NULL if
 * no such procnode exists within the procnode_list 
 */
struct procnode* procnode_list_find(struct procnode_list* pn_list, pid_t pid);

/* Add a procnode to a procnode_list */
void procnode_list_add(struct procnode_list* pn_list, struct procnode* procnode);

/* Remove a procnode from a procnode_list.  The removed procnode list is
 * destroyed. 
 */
void procnode_list_remove(struct procnode_list* pn_list, struct procnode* procnode);
#endif
