/*
 * Copyright (c) 2000, 2001, 2002, 2003, 2004, 2005, 2008, 2009
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
#include <mips/trapframe.h>
#include <limits.h>
#include <copyinout.h>
#include <syscall.h>
#include <pid.h>
#include <proclist.h>
#include <synch.h>
#include <current.h>
#include <addrspace.h>
#include <thread.h>
#include <proc.h>
#include <kern/wait.h>
#include <kern/errno.h>
#include <filetable.h>

int
sys_fork(struct trapframe* tf, pid_t* retval) {
	int result;
	pid_t cp_pid;
	const char *cp_name;
	const char *cp_t_name;
	struct trapframe *cp_tf;
	struct addrspace *p_as;
	struct addrspace *cp_as;
	struct filetable *cp_filetable;
	struct proc *child_proc;
	void **argv;

	*retval = -1;

	cp_name = (const char*)curproc->p_name;
	cp_t_name = (const char*)curthread->t_name;
	
	result = proc_create_user(cp_name, &child_proc);
	if (child_proc == NULL) {
		return result;
	}
	
	child_proc->p_parent = curproc;

	result = proclist_add(curproc->p_children, child_proc);
	if (result) {
		kfree(child_proc);
		panic("proclist_add in sys_fork failed\n");
	}

	cp_pid = child_proc->p_pid;

        spinlock_acquire(&curproc->p_lock);
        if (curproc->p_cwd != NULL) {
                VOP_INCREF(curproc->p_cwd);
                child_proc->p_cwd = curproc->p_cwd;
        }
        spinlock_release(&curproc->p_lock);

	p_as = proc_getas();
	as_copy(p_as, &cp_as);
	cp_filetable = filetable_copy(curproc->p_filetable);
	if (cp_filetable == NULL) {
		return ENOMEM;
	}
	child_proc->p_filetable = cp_filetable;
	cp_tf = kmalloc(sizeof(*tf));
	memcpy(cp_tf, tf, sizeof(*tf));
	cp_tf->tf_v0 = 0;
	cp_tf->tf_a3 = 0;
	cp_tf->tf_epc+=4;
/*
	cp_tf.tf_vaddr = tf->tf_vaddr;
        cp_tf.tf_status = tf->tf_status;
        cp_tf.tf_cause = tf->tf_cause;
        cp_tf.tf_lo = tf->tf_lo;
        cp_tf.tf_hi = tf->tf_hi;
        cp_tf.tf_ra = tf->tf_ra;         
        cp_tf.tf_at = tf->tf_at;         
        cp_tf.tf_v0 = 0;     
        cp_tf.tf_v1 = tf->tf_v1;    
        cp_tf.tf_a0 = tf->tf_a0;
        cp_tf.tf_a1 = tf->tf_a1;
        cp_tf.tf_a2 = tf->tf_a2;
        cp_tf.tf_a3 = 0;
        cp_tf.tf_t0 = tf->tf_t0;
        cp_tf.tf_t1 = tf->tf_t1;
        cp_tf.tf_t2 = tf->tf_t2;
        cp_tf.tf_t3 = tf->tf_t3;
        cp_tf.tf_t4 = tf->tf_t4;
        cp_tf.tf_t5 = tf->tf_t5;
        cp_tf.tf_t6 = tf->tf_t6;
        cp_tf.tf_t7 = tf->tf_t7;
        cp_tf.tf_s0 = tf->tf_s0;
        cp_tf.tf_s1 = tf->tf_s1;
        cp_tf.tf_s2 = tf->tf_s2;
        cp_tf.tf_s3 = tf->tf_s3;
        cp_tf.tf_s4 = tf->tf_s4;
        cp_tf.tf_s5 = tf->tf_s5;
        cp_tf.tf_s6 = tf->tf_s6;
        cp_tf.tf_s7 = tf->tf_s7;
        cp_tf.tf_t8 = tf->tf_t8;
        cp_tf.tf_t9 = tf->tf_t9;
        cp_tf.tf_k0 = tf->tf_k0;        
        cp_tf.tf_k1 = tf->tf_k1;       
        cp_tf.tf_gp = tf->tf_gp;
//        cp_tf.tf_sp = tf->tf_sp;
        cp_tf.tf_s8 = tf->tf_s8;
        cp_tf.tf_epc = tf->tf_epc + 4;   
*/



	argv = kmalloc(2*sizeof(void*));
	argv[0] = cp_tf;
	argv[1] = cp_as;

        result = thread_fork(cp_t_name ,
                        child_proc ,
                        enter_forked_process ,
                        argv, 2);

/*
	result = thread_fork(cp_t_name ,
			child_proc ,
			enter_forked_process ,
			argv, 2);
*/
        if (result) {
                proc_destroy(child_proc);
                return result;
        }

	*retval = cp_pid;
	return 0;
}

int
sys_getpid(pid_t *retval) {
	*retval = curproc->p_pid;
	return 0;
}

int
sys_waitpid(pid_t pid, userptr_t status, int options, pid_t* retval) {
	int *kbuf;
	int result;
	struct procnode *childnode;

	*retval = -1;

	kbuf = kmalloc(sizeof(*kbuf));
	if (kbuf == NULL) {
		return ENOMEM;
	}
	
	if (options != 0 && options != WNOHANG) {
		return EINVAL;

	} else if (!find_pid(pid)){
		return ESRCH;

	} else {
		childnode = proclist_find(curproc->p_children, pid);
		if (childnode == NULL) {
			return ECHILD;

		} else if (childnode->exited) {
			*retval = childnode->exitcode;
			return 0;		

		} else if (options == WNOHANG) {
			*retval = 0;
			return 0;
		}

		P(childnode->waitsem);
		*kbuf = childnode->exitcode;

		result = copyout(kbuf, status, sizeof(*kbuf));
		if (result) {
			proclist_remove(curproc->p_children, childnode);
			kfree(kbuf);
			return result;
		}

		proclist_remove(curproc->p_children, childnode);
		kfree(kbuf);

		*retval = pid;
		return 0; 
	}
}

void sys__exit(int exitcode) {
	struct procnode *procnode;

	if (curproc->p_parent != NULL) {
		procnode = proclist_find(curproc->p_parent->p_children,
curproc->p_pid);

		if (procnode == NULL) {
			panic("proclist_find in sys__exit failed\n");
		}

		procnode->exitcode = _MKWAIT_EXIT(exitcode);
		procnode->exited = 1; 
		V(procnode->waitsem);		
	}	
	
	thread_exit();
	proc_destroy(curproc);		
}
