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

/*
 * pidtable_bootstrap
 *
 * Used to initialize the pidtable upon bootup
 */
void
pidtable_bootstrap(void) {
	pidtable = kmalloc(sizeof(*pidtable));
	if (pidtable == NULL) {
		panic("pidtable_bootstrap failed\n");
	}

	pidtable->num_pidsissued = 0;
	pidtable->num_pidsfreed = 0;
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

/*
 * find_pid
 *
 * Used to determine if a user process with PID is still active
 */
bool
find_pid(pid_t pid) {
	KASSERT(pidtable != NULL);

	lock_acquire(pidtable->pid_lock);
	if (pid > pidtable->last_pid) {
		/* If pid is greater than last_pid, then a process with pid does
		 * not even exist. */
		lock_release(pidtable->pid_lock);
		return false;
	} else if (pidlist_find(pidtable->freed_pids, pid)) {
		/* If pid is amongst the list of other pids which are not being
		 * used, then the user process with pid is no longer active. */
		lock_release(pidtable->pid_lock);
		return false;	
	} else {
		/* The process with pid is still active. */
		lock_release(pidtable->pid_lock);
		return true;
	}
}

/* 
 * get_pid
 *
 * Used to obtain a pid for a newly created user process
 */
int
get_pid(pid_t* pid) {
	KASSERT(pidtable != NULL);

	/* If possible, we want to re-use PIDs which have been freed by exited
	 * user processes.  If no user processes have exited yet, then we get a new pid
	 * number which has never been issued to any user process before. */
	if (pidlist_isempty(pidtable->freed_pids)) {
		if (pidtable->last_pid == PID_MAX) {
			/* Return ENPROC if the maximum number of processes in
			 * the system has been reached. */
			return ENPROC;
		} else {
			/* Get a new pid number which has never been assigned
			 * before */
			lock_acquire(pidtable->pid_lock);
			pidtable->last_pid++;
			*pid = pidtable->last_pid;
			pidtable->num_pidsissued++;
			lock_release(pidtable->pid_lock);
			return 0;
		}
	} else {
		/* Get a pid which has already been used by a now-deceased user
		 * process. Note that instead of incrementing num_pidsissued, we decrement
		 * num_pidsfreed.  The number of pids which have ever issued has not changed
		 * since we are just re-using an old pid.  */
		lock_acquire(pidtable->pid_lock);
		*pid = pidlist_remhead(pidtable->freed_pids);
		pidtable->num_pidsfreed--;
		lock_release(pidtable->pid_lock);
		return 0;
	}
}

/*
 * free_pid
 *
 * Frees pid so it can be re-used by another user process
 */
int
free_pid(pid_t pid) {
	KASSERT(pidtable != NULL);
	KASSERT(pid >= PID_MIN && pid <= pidtable->last_pid);

	int result;
	
	/* Add pid to the pidlist of other pids which have been freed */
	lock_acquire(pidtable->pid_lock);
	result = pidlist_addtail(pidtable->freed_pids, pid);
	if (result) {
		return result;
	}
	
	/* Increment num_pidsfreed */
	pidtable->num_pidsfreed++;
	KASSERT(pidtable->num_pidsissued >= pidtable->num_pidsfreed);

	/* If the total number of pids which have ever been issued equals to the
	 * number of pids which have been freed, then the pidtable can be reset as
	 * though no user processes have ever been created. This ensures that the list
	 * of freed pids does not continue to grow needlessly.*/
	if (pidtable->num_pidsissued == pidtable->num_pidsfreed) {
		pidtable->last_pid = PID_MIN - 1;
		pidlist_clean(pidtable->freed_pids);
		pidtable->num_pidsissued = 0;
		pidtable->num_pidsfreed = 0;
	}

	lock_release(pidtable->pid_lock);
	return result;	
}
