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
#include <proclist.h>

struct procnode *
procnode_init(void) {
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

	procnode->pn_refcount = 2;
	procnode->pid = PID_MIN - 1;
	procnode->exitcode = 0;
	procnode->next = NULL;
	procnode->previous = NULL;
	return procnode;
}

struct proclist *
proclist_init(void) {
	struct proclist *plist;

	plist = kmalloc(sizeof(*plist));
	if (plist == NULL) {
		return NULL;
	}

	plist->head = NULL;
	return plist;
}

bool
proclist_isempty(struct proclist* plist) {
	KASSERT(plist != NULL);

	if (plist->head == NULL) {
		return true;
	} else {
		return false;
	}
}

void
proclist_destroy(struct proclist* plist) {
	KASSERT(plist != NULL);

	struct procnode *itvar;
	struct procnode *rem_procnode;

	if (!proclist_isempty(plist)) {
		itvar = plist->head;

		while (itvar != NULL) {
			KASSERT(itvar->pn_refcount != 0);

			if (itvar->pn_refcount == 1) {
				rem_procnode = itvar;
				itvar = itvar->next;
				proclist_remove(plist, rem_procnode);
			} else {
				lock_acquire(itvar->pn_lock);
				if (itvar->pn_refcount == 1) {
					lock_release(itvar->pn_lock);
					rem_procnode = itvar;
					itvar = itvar->next;
					proclist_remove(plist, rem_procnode);
				} else {
					itvar->pn_refcount--;
					lock_release(itvar->pn_lock);
				}
			}
		}
	}

	kfree(plist);
}


struct procnode *
proclist_find(struct proclist* plist, pid_t pid) {
	struct procnode *itvar;

	if (plist == NULL) {
		return NULL;

	} else if (proclist_isempty(plist)) {
		return NULL;
	
	} else if (pid < PID_MIN || pid > PID_MAX) {
		return NULL;

	} else { 
		for (itvar = plist->head; itvar != NULL; itvar = itvar->next) {
			if (itvar->pid == pid) {
				return itvar;
			}	
		}

		return NULL;
	}
}

void
proclist_add(struct proclist* plist, struct procnode* procnode) {
	KASSERT(plist != NULL);
	KASSERT(procnode != NULL);

	procnode->next = plist->head;
	
	if (plist->head != NULL) {
		plist->head->previous = procnode;
	}

	plist->head = procnode;
}

void
proclist_remove(struct proclist* plist, struct procnode* procnode) {
	KASSERT(plist != NULL);
	KASSERT(procnode != NULL);
	
	if (plist->head == procnode) {
		plist->head = procnode->next;
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
