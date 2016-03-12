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
#include <lib.h>
#include <limits.h>
#include <proc.h>
#include <current.h>
#include <synch.h>
#include <kern/errno.h>
#include <procnode_list.h>

/*
 * procnode_create
 *
 * Creates and initialize a new procnode
 */
struct procnode *
procnode_create(void) {
	struct procnode *procnode;

	procnode = kmalloc(sizeof(*procnode));
	if (procnode == NULL) {
		return NULL;
	}

        procnode->waitsem = sem_create("wait semaphore", 0);
        if (procnode->waitsem == NULL) {
		kfree(procnode);
                return NULL;
        }

	procnode->pn_lock = lock_create("procnode lock");
	if (procnode->pn_lock == NULL) {
		kfree(procnode);
		return NULL;
	}

	/* Set the reference count to be 2 since the parent process and the
	 * child process initially both point to it. */
	procnode->pn_refcount = 2;

	/* Set the pid field to fall outside of the range of possible user
	 * process pids */
	procnode->pid = PID_MIN - 1;

	/* Initialize the exitcode field to 0 */
	procnode->exitcode = 0;

	procnode->next = NULL;
	procnode->previous = NULL;
	return procnode;
}

/*
 * procnode_list_create
 *
 * Creates a procnode_list
 */
struct procnode_list *
procnode_list_create(void) {
	struct procnode_list *pn_list;

	pn_list = kmalloc(sizeof(*pn_list));
	if (pn_list == NULL) {
		return NULL;
	}

	pn_list->head = NULL;
	return pn_list;
}

/*
 * procnode_list_isempty
 *
 * Checks if a procnode_list is empty
 */
bool
procnode_list_isempty(struct procnode_list* pn_list) {
	KASSERT(pn_list != NULL);

	if (pn_list->head == NULL) {
		return true;
	} else {
		return false;
	}
}

/*
 * procnode_list_desetroy
 *
 * Destroys the procnode_list
 */
void
procnode_list_destroy(struct procnode_list* pn_list) {
	KASSERT(pn_list != NULL);

	struct procnode *itvar;
	struct procnode *rem_procnode;

	if (!procnode_list_isempty(pn_list)) {
		itvar = pn_list->head;

		while (itvar != NULL) {
			KASSERT(itvar->pn_refcount != 0);

			if (itvar->pn_refcount == 1) {
				/* If the reference count of the procnode is
				 * equal to 1, the current process is the last
				 * one to be pointing to that procnode.  So the
				 * current process is free to destroy it. */
				rem_procnode = itvar;
				itvar = itvar->next;
				procnode_destroy(rem_procnode);
			} else {
				lock_acquire(itvar->pn_lock);
				if (itvar->pn_refcount == 1) {
					/* While acquiring the procnode lock,
					 * the other process pointing to the
					 * procnode has exited.  Therefore, the
					 * current process is free to destroy
					 * it. */
					lock_release(itvar->pn_lock);
					rem_procnode = itvar;
					itvar = itvar->next;
					procnode_destroy(rem_procnode);
				} else {
					/* If we reached this point in
					 * procnode_list_destroy, there is
					 * another process pointing to the
					 * procnode.  Therefore, we don't
					 * destroy the procnode.  We instead
					 * decrement the procnode's reference
					 * counter. */
					itvar->pn_refcount--;
					lock_release(itvar->pn_lock);
				}
			}
		}
	}

	/* Free the procnode_list */
	kfree(pn_list);
}

/*
 * procnode_destroy
 *
 * Destroys the procnode
 */
void
procnode_destroy(struct procnode* procnode) {
	lock_destroy(procnode->pn_lock);
	sem_destroy(procnode->waitsem);
	kfree(procnode);
}

/*
 * procnode_list_find
 *
 * Finds the procnode node in the procnode_list which contains the pid
 */
struct procnode *
procnode_list_find(struct procnode_list* pn_list, pid_t pid) {
	struct procnode *itvar;

	if (pn_list == NULL) {
		/* Return NULL if the procnode list does not exist */
		return NULL;

	} else if (procnode_list_isempty(pn_list)) {
		/* Return NULL if the procnode list is empty */
		return NULL;
	
	} else if (pid < PID_MIN || pid > PID_MAX) {
		/* Return NULL if the pid falls outside the range of possible
		 * user process pids */
		return NULL;

	} else { 
		/* Iterate through each procnode in the procnode list until we
		 * find a procnode containing the pid */
		for (itvar = pn_list->head; itvar != NULL; itvar = itvar->next) {
			if (itvar->pid == pid) {
				return itvar;
			}	
		}

		/* Return NULL if we iterated through the entire procnode list
		 */
		return NULL;
	}
}

/*
 * procnode_list_add
 *
 * Adds a new procnode to the head of the procnode_list
 */
void
procnode_list_add(struct procnode_list* pn_list, struct procnode* procnode) {
	KASSERT(pn_list != NULL);
	KASSERT(procnode != NULL);

	procnode->next = pn_list->head;
	
	if (pn_list->head != NULL) {
		pn_list->head->previous = procnode;
	}

	pn_list->head = procnode;
}

/*
 * procnode_list_remove
 *
 * Removes and destroys a procnode from a procnode_list
 */
void
procnode_list_remove(struct procnode_list* pn_list, struct procnode* procnode) {
	KASSERT(pn_list != NULL);
	KASSERT(procnode != NULL);
	
	if (pn_list->head == procnode) {
		pn_list->head = procnode->next;
	}

	if (procnode->previous != NULL) {
		procnode->previous->next = procnode->next;
	}

	if (procnode->next != NULL) {
		procnode->next->previous = procnode->previous;
	}

	lock_destroy(procnode->pn_lock);
	sem_destroy(procnode->waitsem);
	kfree(procnode);
}

