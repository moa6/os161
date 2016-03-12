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

#ifndef _PID_H_
#define _PID_H_

#include <types.h>
#include <lib.h>
#include <pidlist.h>

/* pidtable struct:
 * The pidtable struct is a struct used by processes to obtain their PIDs.
 */
struct pidtable {
	struct lock *pid_lock;		/* Lock to ensure that PIDs are obtained
					   and freed atomically */

	unsigned num_pidsissued;	/* Total number of PIDs which have ever
					   been issued */

	unsigned num_pidsfreed;		/* Total number of PIDs which have been
					   freed */

	pid_t last_pid;			/* The maximum PID value issued thus far
					 */

	struct pidlist *freed_pids;	/* A pidlist of PIDs which have been
					   freed */
};

/* The pidtable is a global structure since it needs to be accessed by all user
 * processes. */
extern struct pidtable *pidtable;

/* Initialize the pidtable upon bootup */
void pidtable_bootstrap(void);

/* Determine if a user process with pid is still active */
bool find_pid(pid_t pid);

/* Used to assign PIDs to newly created user processes */
int get_pid(pid_t* pid);

/* Used to free the PID of an exiting user process.  The freed PID can then be
 * used again by a newly created user process.
 */
int free_pid(pid_t pid);

#endif
