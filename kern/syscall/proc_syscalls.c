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
execv(char *progname, char **user_arg)
{
        struct addrspace *as;
        struct vnode *v;
        vaddr_t entrypoint, stackptr;
        int result;

//acquire lock
        lock_acquire(execv);

//copy agrument to store temporarily

        if (program == NULL || user_arg == NULL ) {
                //kprintf("EXECV- Argument is a null pointer\n");
                return EFAULT;
        }

        char *progname;
        size_t size;

        progname = (char *) kmalloc(sizeof(char) * MAX_DEF);

        result = copyinstr((const_userptr_t) program, progname, MAX_DEF, &size);

        if (result) {
                //fail message
                kfree(progname);
                return EFAULT;
        }

        if (size == 1) {
                //kprintf("Empty Program Name\n");
                kfree(progname);
                return EINVAL;
        }

        char **args = (char **) kmalloc(sizeof(char **));
    result = copyin((const_userptr_t) user_arg, args, sizeof(char **));

        if (result) {
                //failure
                kfree(progname);
                kfree(args);
                return EFAULT;
        }


        //allocate memory for argument
        while (user_arg[i] != NULL ) {
                args[i] = (char *) kmalloc(sizeof(char) * MAX_DEF);
                result = copyinstr((const_userptr_t) user_arg[i], args[i], MAX_DEF,
                                &size);
                if (result) {
                        kfree(progname);
                        kfree(args);
                        return EFAULT;
                }
                i++;
        }
        args[i] = NULL;




        /* Open the file. */
        result = vfs_open(progname, O_RDONLY, 0, &v);
        if (result) {
                kfree(progname);
                kfree(args);
                return result;
        }

        /* We should be a new process. */
        KASSERT(proc_getas() == NULL);

        /* Create a new address space. */
        as = as_create();
        if (as == NULL) {
                kfree(progname);
                kfree(args);
             return result;
        }

        /* We should be a new process. */
        KASSERT(proc_getas() == NULL);

        /* Create a new address space. */
        as = as_create();
        if (as == NULL) {
                kfree(progname);
                kfree(args);
                vfs_close(v);
                return ENOMEM;
        }

        /* Switch to it and activate it. */
        proc_setas(as);

      as_activate();

        /* Load the executable. */
        result = load_elf(v, &entrypoint);
        if (result) {
                /* p_addrspace will go away when curproc is destroyed */
                kfree(progname);
                kfree(args);
                vfs_close(v);
                return result;
        }

        /* Done with the file now. */
        vfs_close(v);

        /* Define the user stack in the address space */
        result = as_define_stack(as, &stackptr);
        if (result_end) {

                kfree(progname);
                kfree(args);
                return result;
        }

/////Copy out the temporary stored argument to address create stack//
///not implemented

        lock_release(execv);

        /* Warp to user mode. */
        enter_new_process(0 /*argc*/, NULL /*userspace addr of argv*/,
                          NULL /*userspace addr of environment*/,
                          stackptr, entrypoint);

        /* enter_new_process does not return. */
        panic("enter_new_process returned\n");
        return EINVAL;

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
	struct procnode *child_procnode;
	void **argv;

	*retval = -1;

	cp_name = (const char*)curproc->p_name;
	cp_t_name = (const char*)curthread->t_name;
	
	result = proc_create_user(cp_name, &child_proc);
	if (child_proc == NULL) {
		return result;
	}
	
	cp_pid = child_proc->p_pid;

	child_procnode = procnode_init();
	child_procnode->pid = cp_pid;
	proclist_add(curproc->p_children, child_procnode);
	child_proc->p_parent = child_procnode;

        spinlock_acquire(&curproc->p_lock);
        if (curproc->p_cwd != NULL) {
                VOP_INCREF(curproc->p_cwd);
                child_proc->p_cwd = curproc->p_cwd;
        }
        spinlock_release(&curproc->p_lock);

	p_as = proc_getas();

	result = as_copy(p_as, &cp_as);
	if (result) {
		return result;
	}

	if (curproc->p_filetable != NULL) {
		cp_filetable = filetable_copy(curproc->p_filetable);
		if (cp_filetable == NULL) {
			return ENOMEM;
		}
	}
	child_proc->p_filetable = cp_filetable;
	cp_tf = kmalloc(sizeof(*tf));
	memcpy(cp_tf, tf, sizeof(*tf));
	cp_tf->tf_v0 = 0;
	cp_tf->tf_a3 = 0;
	cp_tf->tf_epc+=4;

	argv = kmalloc(2*sizeof(void*));
	argv[0] = cp_tf;
	argv[1] = cp_as;

        result = thread_fork(cp_t_name ,
                        child_proc ,
                        enter_forked_process ,
                        argv, 2);

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

		} else if (options == WNOHANG && childnode->pn_refcount > 1) {
			*retval = 0;
			return 0;

		} else if (childnode->pn_refcount == 1) {
			*kbuf = childnode->exitcode;
			
			if (status != NULL) {
				result = copyout(kbuf, status, sizeof(*kbuf));
				if (result) {
					kfree(kbuf);
					return result;
				}
		 	}

			proclist_remove(curproc->p_children, childnode);
			kfree(kbuf);			

			result = free_pid(pid);
			if (result) {
				return result;				
			}

			*retval = pid;
			return 0;

		} else {
			P(childnode->waitsem);
			*kbuf = childnode->exitcode;

			if (status != NULL) {
				result = copyout(kbuf, status, sizeof(*kbuf));
				if (result) {
					kfree(kbuf);
					return result;
				}
		 	}

			proclist_remove(curproc->p_children, childnode);
			kfree(kbuf);

			result = free_pid(pid);		
			if (result) {
				return result;
			}
	
			*retval = pid;
			return 0;
		} 
	}
}

void sys__exit(int exitcode) {
	int result;
	struct procnode *procnode;
	struct proc *cur_p;
	struct thread *cur_t;

	procnode = curproc->p_parent;

	if (procnode != NULL) {
		KASSERT(procnode->pid == curproc->p_pid);
	
		if (procnode->pn_refcount > 1) {
			lock_acquire(procnode->pn_lock);
			if (procnode->pn_refcount == 1) {
				lock_release(procnode->pn_lock);
				result = free_pid(procnode->pid);
				if (result) {
					kfree(procnode);
					panic("free_pid in sys__exit failed\n");
				}
				kfree(procnode);
			} else {
				procnode->exitcode = _MKWAIT_EXIT(exitcode);				
				procnode->pn_refcount--;
				lock_release(procnode->pn_lock);
				V(procnode->waitsem);
			}			
		} else {
			result = free_pid(procnode->pid);
			if (result) {
				kfree(procnode);
				panic("free_pid in sys__exit failed\n");
			}
			kfree(procnode);
		}

	} else {
			result = free_pid(curproc->p_pid);
			if (result) {
				panic("free_pid in sys__exit failed\n");
			}
	}

	filetable_destroy(curproc->p_filetable);
	cur_p = curproc;
	cur_t = curthread;
	proc_remthread(cur_t);
	proc_destroy(cur_p);	
        thread_exit();	
}
