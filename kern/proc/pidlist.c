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
#include <current.h>
#include <kern/errno.h>
#include <pidlist.h>

/*
 * pidlist_init
 *
 * Create and initialize the pidlist
 */
struct pidlist *
pidlist_init(void) {
	struct pidlist *plist;

	plist = kmalloc(sizeof(*plist));
	if (plist == NULL) {
		return NULL;
	}

	plist->head = NULL;
	plist->tail = NULL;
	return plist;
}

/*
 * pidlist_isempty
 *
 * Determines if the pidlist is empty 
 */
bool
pidlist_isempty(struct pidlist* plist) {
	KASSERT(plist != NULL);

	if (plist->head == NULL && plist->tail == NULL) {
		/* If the head and tail of the pidlist are NULL, the pidlist is
 		 * empty */
		return true;
	} else {
		return false;
	}
}

/*
 * pidlist_find
 *
 * Finds the pidnode in the pidlist which contains the pid
 */
bool
pidlist_find(struct pidlist* plist, pid_t pid) {
	struct pidnode *itvar;

	if (plist == NULL) {
		/* Return false if there is no pidlist */
		return false;

	} else if (pidlist_isempty(plist)) {
		/* Return false if the pidlist is empty */
		return false;

	} else if (pid < PID_MIN || pid > PID_MAX) {
		/* Return false if pid is not in a valid range */
		return false;

	} else {
		/* Iterate through each pidnode in the pidlist until we find a
		 * procnode containing the pid */
		for (itvar = plist->head; itvar != NULL; itvar = itvar->next) {
			if (itvar->pid == pid) {
				return true;
			}
		}

		/* Return false if we iterated through the entire pidlist */
		return false;
	}
}

/*
 * pidlist_remhead
 *
 * Returns the pid of the pidnode at the head of the pidlist.  The head of the
 * pidlist is destroyed. 
 */
pid_t
pidlist_remhead(struct pidlist* plist) {
	KASSERT(plist != NULL);
	KASSERT(plist->head != NULL);

	int pid;
	struct pidnode *p_head;

	p_head = plist->head;
	pid = p_head->pid;
	plist->head = p_head->next;
	kfree(p_head);

	if (plist->head == NULL) {
		plist->tail = NULL;
	}

	return pid;
}

/*
 * pidlist_addtail
 *
 * Adds a new pidnode with pid to the end of the pidlist
 */
int
pidlist_addtail(struct pidlist* plist, int pid) {
	KASSERT(plist != NULL);
	KASSERT(pid >= PID_MIN && pid <= PID_MAX); 

	struct pidnode *p_tail;

	p_tail = plist->tail;	
	if (p_tail == NULL) {
		KASSERT(plist->head == NULL);

		p_tail = kmalloc(sizeof(*p_tail));
		if (p_tail == NULL) {
			return ENOMEM;
		}

		p_tail->next = NULL;
		p_tail->pid = pid;
		plist->head = p_tail;
		plist->tail = p_tail;
	} else {
		p_tail->next = kmalloc(sizeof(*p_tail));
		if (p_tail->next == NULL) {
			return ENOMEM;
		}

		p_tail = p_tail->next;
		p_tail->pid = pid;
		p_tail->next = NULL;
		plist->tail = p_tail;
	}

	return 0;
}

/*
 * pidlist_clean
 *
 * Cleans the pidlist
 */
void
pidlist_clean(struct pidlist* plist) {
	KASSERT(plist != NULL);

	struct pidnode *itvar;
	struct pidnode *p_rem;

	itvar = plist->head;

	/* Iterate through the entire pidlist and free the pidnodes */
	while (itvar != NULL) {
		p_rem = itvar;
		itvar = itvar->next;
		kfree(p_rem);
	}

	plist->head = NULL;
	plist->tail = NULL;
}
