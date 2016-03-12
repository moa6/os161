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

#ifndef _PIDLIST_H_
#define _PIDLIST_H_

#include <types.h>
#include <lib.h>

/* pidnode struct:
 * The pidnode struct contains a pid which can be assigned to a newly created
 * user process.
 */
struct pidnode {
	pid_t pid; 		/* pid available to be assigned */
	struct pidnode *next;   /* Pointer to the next pidnode in a pidlist */
};

/* pidlist struct:
 * A list of pidnodes
 */
struct pidlist {
	struct pidnode *head;	/* Head of the pidlist */
	struct pidnode *tail;	/* Tail of the pidlist */
};

/* Create and initialize a pidlist */
struct pidlist* pidlist_init(void);

/* Checks if a pidlist is empty of pidnodes */
bool pidlist_isempty(struct pidlist* plist);

/* Determines if a procnode containing pid exists in a pidlist */
bool pidlist_find(struct pidlist *plist, pid_t pid);

/* Returns the pid from the head of the pidlist.  The head of the pidlist is
 * destroyed. */
pid_t pidlist_remhead(struct pidlist* plist);

/* Add pid to the pidlist */
int pidlist_addtail(struct pidlist* plist, int pid);

/* Clean up the pidlist */
void pidlist_clean(struct pidlist *plist);
#endif
