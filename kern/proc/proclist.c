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

	procnode->exitcode = 0;
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

	if (!proclist_isempty(plist)) {
		for (itvar = plist->head; itvar->next != NULL; itvar = itvar->next) {
			sem_destroy(itvar->waitsem);
			itvar->proc = NULL;
			
			if (itvar->previous != NULL) {
				kfree(itvar->previous);
			}
		}

		sem_destroy(itvar->waitsem);
		itvar->proc = NULL;
		kfree(itvar);
		kfree(plist);
	}
}


struct procnode *
proclist_find(struct proclist* plist, pid_t pid) {
	KASSERT(plist != NULL);
	KASSERT(pid >= PID_MIN && pid <= PID_MAX);

	struct procnode *itvar;

	for (itvar = plist->head; itvar != NULL; itvar = itvar->next) {
		if (itvar->proc->p_pid == pid) {
			return itvar;
		}
	}

	return NULL;
}

int
proclist_add(struct proclist* plist, struct proc* process) {
	KASSERT(plist != NULL);
	KASSERT(process != NULL);

	struct procnode *new_procnode;
	
	new_procnode = procnode_init();
	if (new_procnode == NULL) {
		return 1;
	}

	new_procnode->proc = process;
	new_procnode->next = plist->head;
	new_procnode->previous = NULL;
	
	if (plist->head != NULL) {
		plist->head->previous = new_procnode;
	}

	plist->head = new_procnode;
	return 0;
}

void
proclist_remove(struct proclist* plist, struct procnode* procnode) {
	KASSERT(plist != NULL);
	KASSERT(procnode != NULL);

	if (procnode->previous != NULL) {
		procnode->previous->next = procnode->next;
	}

	if (procnode->next != NULL) {
		procnode->next->previous = procnode->previous;
	}

	kfree(procnode);
}
