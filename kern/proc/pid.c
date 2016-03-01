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
#include <lib.h>
#include <limits.h>
#include <synch.h>
#include <kern/errno.h>
#include <pid.h>

struct pidtable *pidtable;

void
pidtable_bootstrap(void) {
	pidtable = kmalloc(sizeof(*pidtable));
	if (pidtable == NULL) {
		panic("pidtable_bootstrap failed\n");
	}

	pidtable->last_pid = PID_MIN - 1;
	pidtable->freed_pids = pidlist_init();
	if (pidtable->freed_pids == NULL) {
		kfree(pidtable);
		panic("pidlist_init failed in pidtable_bootstrap\n");
	}

	pidtable->pid_lock = lock_create("pid lock");
	if (pidtable->pid_lock == NULL) {
		kfree(pidtable);
		panic("lock_create failed in pidtable_bootstrap\n");
	}
}

bool
find_pid(pid_t pid) {
	KASSERT(pidtable != NULL);

	lock_acquire(pidtable->pid_lock);
	if (pid > pidtable->last_pid) {
		lock_release(pidtable->pid_lock);
		return false;
	} else if (pidlist_find(pidtable->freed_pids, pid)) {
		lock_release(pidtable->pid_lock);
		return false;	
	} else {
		lock_release(pidtable->pid_lock);
		return true;
	}
}

int
get_pid(pid_t* pid) {
	KASSERT(pidtable != NULL);

	if (pidlist_isempty(pidtable->freed_pids)) {
		if (pidtable->last_pid == PID_MAX) {
			return ENPROC;
		} else {
			lock_acquire(pidtable->pid_lock);
			pidtable->last_pid++;
			*pid = pidtable->last_pid;
			lock_release(pidtable->pid_lock);
			return 0;
		}
	} else {
		lock_acquire(pidtable->pid_lock);
		*pid = pidlist_remhead(pidtable->freed_pids);
		lock_release(pidtable->pid_lock);
		return 0;
	}
}

int
free_pid(pid_t pid) {
	KASSERT(pidtable != NULL);
	KASSERT(pid >= PID_MIN && pid <= pidtable->last_pid);

	int result;
	
	lock_acquire(pidtable->pid_lock);
	result = pidlist_addtail(pidtable->freed_pids, pid);
	lock_release(pidtable->pid_lock);
	return result;	
}
