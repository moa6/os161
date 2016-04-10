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
#include <mips/vm.h>
#include <limits.h>
#include <copyinout.h>
#include <syscall.h>
#include <pid.h>
#include <procnode_list.h>
#include <synch.h>
#include <current.h>
#include <coremap.h>
#include <addrspace.h>
#include <thread.h>
#include <bitmap.h>
#include <swap.h>
#include <proc.h>
#include <kern/wait.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <filetable.h>

/*
 * sys_fork
 *
 * Copy the current process.  
 */
int
sys_fork(struct trapframe* tf, pid_t* retval) {
	int i;
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

	/* Create a child process */
	cp_name = (const char*)curproc->p_name;
	cp_t_name = (const char*)curthread->t_name;
	
	result = proc_create_user(cp_name, &child_proc);
	if (child_proc == NULL) {
		return result;
	}
	
	/* Save the child process's pid in variable cp_pid. The variable cp_pid
	 * is returned to the parent.*/
	cp_pid = child_proc->p_pid;

	/* Create a new procnode which will be pointed to by both the parent and
	 * child processes */
	child_procnode = procnode_create();
	if (child_procnode == NULL) {
		return ENOMEM;
	}

	/* Set the procnode's pid field as the child process's pid */
	child_procnode->pid = cp_pid;
	
	/* The parent adds the procnode to p_children */
	procnode_list_add(curproc->p_children, child_procnode);

	/* The child process points to the procnode via p_parent */
	child_proc->p_parent = child_procnode;

	/* Ensure that the parent and child processes share the same cwd */
        spinlock_acquire(&curproc->p_lock);
        if (curproc->p_cwd != NULL) {
                VOP_INCREF(curproc->p_cwd);
                child_proc->p_cwd = curproc->p_cwd;
        }
        spinlock_release(&curproc->p_lock);

	/* Copy the parent's address space */
	p_as = proc_getas();
	result = as_copy(p_as, &cp_as);
	if (result) {
		return result;
	}

	/* Copy the parent's file table and assign it to the child process */
	if (curproc->p_filetable != NULL) {
		cp_filetable = filetable_copy(curproc->p_filetable);
		if (cp_filetable == NULL) {
			return ENOMEM;
		}
	}
	child_proc->p_filetable = cp_filetable;

	/* Copy the parent's trapframe for the child.  The only difference
	 * between the child's trap frame and the parent's trapframe is the return value
	 * and the error number. */
	cp_tf = kmalloc(sizeof(*tf));
	memcpy(cp_tf, tf, sizeof(*tf));
	cp_tf->tf_v0 = 0;
	cp_tf->tf_a3 = 0;

	/* Increment the child trapframe's program counter so the child does not
	 * call fork */
	cp_tf->tf_epc+=4;


	/* Fork the current thread */
	argv = kmalloc(2*sizeof(void*));
	if (argv == NULL) {
		return ENOMEM;
	}

	argv[0] = cp_tf;
	argv[1] = cp_as;

        result = thread_fork(cp_t_name ,
                        child_proc ,
                        enter_forked_process ,
                        argv, 2);

        if (result) {
        	for (i=0; i<=child_proc->p_filetable->last_fd; i++) {
                	if (child_proc->p_filetable->entries[i] != NULL) {
                       	 	if (child_proc->p_filetable->entries[i]->f_refcount > 1) {
					lock_acquire(child_proc->p_filetable->entries[i]->f_lock);

					if (child_proc->p_filetable->entries[i]->f_refcount > 1) {
						child_proc->p_filetable->entries[i]->f_refcount--;
						lock_release(child_proc->p_filetable->entries[i]->f_lock);
						child_proc->p_filetable->entries[i]
						= NULL;

					} else {
						lock_release(child_proc->p_filetable->entries[i]->f_lock);
						vfs_close(child_proc->p_filetable->entries[i]->vn);
					}

				} else {
					vfs_close(child_proc->p_filetable->entries[i]->vn);
				}
								 
                	}
        	}

                proc_destroy(child_proc);
                return result;
        }

	/* Parent returns from fork with the child's pid value */
	*retval = cp_pid;
	return 0;
}

/*
 * sys_execv
 *
 * Execute a program
 */
int
sys_execv(const_userptr_t program, userptr_t* args, int* retval) {
	int argc;
	int result;
	int strlen;
	int aligned_strlen;
	int total_strlen;
	struct addrspace *oldas;
	struct addrspace *newas;
	struct vnode *v;
	vaddr_t entrypoint, stackbptr, stackptr, char_addr, argv_addr;
	size_t len;
	char **k_args;
	char *k_char;

	*retval = -1;

	/* Copy the pathname of the program into the kernel space */
	char *k_program = kmalloc(PATH_MAX*sizeof(char));
	if (k_program == NULL) {
		return ENOMEM;
	}

	result = copyinstr(program, k_program, PATH_MAX, &len);
	if (result) {
		kfree(k_program);
		return result;
	}

	/* Copy the pointer to the first argument string into the kernel space */
	k_args = kmalloc(sizeof(char*));
	if (k_args == NULL) {
		kfree(k_program);
		return ENOMEM;
	}

	result = copyin((const_userptr_t)args, (void *)k_args, sizeof(char*));
	if (result) {
		kfree(k_program);
		kfree(k_args);
		return result;
	}

	/* Initialize total_strlen and argc to 0 */
	total_strlen = 0;
	argc = 0;

	k_char = kmalloc(sizeof(char));
	if (k_char == NULL) {
		kfree(k_program);
		kfree(k_args);
		return ENOMEM;
	}

	while (k_args[0] != NULL) {
		strlen = 0;
		aligned_strlen = 0;

		/* Determine the string length of each argument.  Each character
		 * in an argument is copied into the kernel space until we hit a NULL character.
		 * The variable strlen keeps track of the string length */
		do {	
			result =
copyin((const_userptr_t)k_args[0]+strlen*sizeof(char),
	k_char, sizeof(char)); 
			if (result) {
				kfree(k_program);
				kfree(k_args);
				kfree(k_char);
				return result;
			}

			strlen++;

		} while (*k_char != '\000');

		/* The arguments must be placed in the user stack in 4 byte
		 * boundaries.  So we calculate how much user stack space each argument needs to
		 * occupy. */
		if (strlen%4 != 0) {
			aligned_strlen = 4*(strlen/4) + 4;
		} else {
			aligned_strlen = strlen;
		}

		/* The total_strlen variable tracks how much user stack space
		 * the strings occupy */
		total_strlen+=aligned_strlen;

		/* Increment argc */
		argc++;

		/* Copy the pointer to the next argument string into the kernel
		 * space.  If the pointer to the next argument string is NULL, we will break out
		 * of the while loop. */
                result = copyin((const_userptr_t)args+sizeof(char*)*argc, (void *)k_args,
sizeof(char*));
                if (result) {
                        kfree(k_program);
			kfree(k_args);
                        kfree(k_char);
                        return result;
                }

	}

	/* Open the program file */
	result = vfs_open(k_program, O_RDONLY, 0, &v);
	if (result) {
		kfree(k_program);
		kfree(k_args);
		kfree(k_char);
		return result;
	}

	kfree(k_program);
        oldas = curproc->p_addrspace;
        newas = as_create();
        proc_setas(newas);
        as_activate();

	/* Load the executable */
	result = load_elf(v, &entrypoint);
	if (result) {
		kfree(k_args);
		kfree(k_char);
		vfs_close(v);
		proc_setas(oldas);
		as_activate();
		as_destroy(newas);
		return result;
	}

	vfs_close(v);

	/* Define the user stack pointer of the new address space */
	result = as_define_stack(newas, &stackptr);
	if (result) {
		kfree(k_args);
		kfree(k_char);
		proc_setas(oldas);
		as_activate();
		as_destroy(newas);
		return result;	
	}

	/* stackbptr is the user stack address which the first argument string
	 * pointer occupies.*/ 
	stackbptr = stackptr - (argc+1)*sizeof(char*) - total_strlen*sizeof(char);

	/* argv_addr is used to place argument string pointers in their
	 * appropriate places within the stack. */
	argv_addr = stackbptr + argc*sizeof(char*);

	/* Place NULL on the user stack to indicate the end of the argument
	 * string pointer array */
	char_addr = 0;
	
	result = copyout(&char_addr, (void*)argv_addr, sizeof(char*));
	if (result) {
		kfree(k_args);
		kfree(k_char);
		proc_setas(oldas);
		as_activate();
		as_destroy(newas);
		return result;	
	}

	/* Set char_addr to the top of the user stack.  From this point
	 * forward, char_addr is used to place string characters in their appropriate
	 * places within the user stack. */
	char_addr = stackptr;

	/* Place each argument string pointer and its corresponding argument
	 * string onto the user stack, starting with the last argument string pointer and
	 * argument sting. */
	for (int i=1; i<=argc; i++) {
		strlen = 0;

		/* Switch to the old address space so we can copy the argument
		 * string pointer into the kernel space */
		proc_setas(oldas);
		as_activate();

		/* Copy the argument string pointer into the kernel space */
                result = copyin((const_userptr_t)args+sizeof(char*)*(argc-i), (void *)k_args,
sizeof(char*));
                if (result) {
			kfree(k_args);
			kfree(k_char);
                        as_destroy(newas);
                        return result;
                }

		/* Determine the length of the argument string */
		do {	
			result =
copyin((const_userptr_t)k_args[0]+strlen*sizeof(char),
	k_char, sizeof(char)); 
			if (result) {
				kfree(k_args);
				kfree(k_char);
				as_destroy(newas);
				return result;
			}

			strlen++;

		} while (*k_char != '\000');

		/* Calculate the user stack address where the first argument
		 * string character should be placed. */
		if (strlen %4 != 0) {
			aligned_strlen = 4*(strlen/4) + 4;
		} else {
			aligned_strlen = strlen;
		}

		char_addr = char_addr - aligned_strlen*sizeof(char);

		/* Copy each of the argument string characters onto the user
		 * stack */
		for (int j=0; j<strlen; j++) {
			/* Copy an argument string character into the kernel
			 * space */
			result =
copyin((const_userptr_t)k_args[0]+j*sizeof(char),
	k_char, sizeof(char)); 
			if (result) {
				kfree(k_args);
				kfree(k_char);
				as_destroy(newas);
				return result;
			}

			/* Switch to the new address space and copy the argument
			 * string character onto the user stack */
			proc_setas(newas);
			as_activate();

			result = copyout(k_char, (void *)char_addr,
sizeof(char));
			if (result) {
				kfree(k_args);
				kfree(k_char);
				proc_setas(oldas);
				as_activate();
				as_destroy(newas);
				return result;
			}

			/* Increment char_addr so that the next argument
			 * string character will be placed properly onto the user stack */
			char_addr = char_addr + sizeof(char);

			/* Switch back to the old address space */
			proc_setas(oldas);
			as_activate();
		}

		/* Place the argument string pointer on the user stack */
		proc_setas(newas);
		as_activate();

		char_addr = char_addr - strlen*sizeof(char);
		argv_addr = argv_addr - sizeof(char*);

		result = copyout(&char_addr, (void *)argv_addr,
sizeof(char*));
		if (result) {
			kfree(k_args);
			kfree(k_char);
			proc_setas(oldas);
			as_activate();
			as_destroy(newas);
			return result;
		}

	}

	/* Free all kernel buffers and destroy the old address space */
	kfree(k_args);
	kfree(k_char);
	as_destroy(oldas);

	/* Enter into the new process */
        enter_new_process(argc, (userptr_t)stackbptr,
                          NULL,
                          stackbptr, entrypoint);

	/* Panic if we return from the new process */
        panic("enter_new_process returned in sys_execv\n");
        return EINVAL;	

}

/*
 * sys_getpid
 *
 * Returns the pid of the current process
 */
int
sys_getpid(pid_t *retval) {
	*retval = curproc->p_pid;
	return 0;
}

/*
 * sys_waitpid
 *
 * Wait for the process specified by pid to exit
 */
int
sys_waitpid(pid_t pid, userptr_t status, int options, pid_t* retval) {
	int *kbuf;
	int result;
	struct procnode *childnode;

	*retval = -1;

	/* Allocate a kernel buffer which will hold the child process's exit
         * code */
	kbuf = kmalloc(sizeof(*kbuf));
	if (kbuf == NULL) {
		/* Return ENOMEM if a kernel buffer cannot be allocated */
		return ENOMEM;
	}
	
	if (options != 0 && options != WNOHANG) {
		/* Return EINVAL if the option is invalid */
		kfree(kbuf);
		return EINVAL;

	} else if (!find_pid(pid)){
		/* Return ESRCH if pid named an inactive process */
		kfree(kbuf);
		return ESRCH;

	} else {
		/* Obtain the procnode which is shared between the parent and
		 * child processes */
		childnode = procnode_list_find(curproc->p_children, pid);
		if (childnode == NULL) {
			/* Return ECHILD if pid named a process which is not a
			 * child of the current process */
			kfree(kbuf);
			return ECHILD;

		} else if (options == WNOHANG && childnode->pn_refcount > 1) {
			/* Return 0 if the option is WNOHANG and the child
			 * process has not yet exited.  If childnode->pn_refcount is greater than 1, the
			 * child process must still be active (ie. not yet exited). */
			*retval = 0;
			return 0;

		} else if (childnode->pn_refcount == 1) {
			/* The child has already exited. Return the exit
			 * status if the status buffer is not NULL */
			*kbuf = childnode->exitcode;
			
			if (status != NULL) {
				result = copyout(kbuf, status, sizeof(*kbuf));
				if (result) {
					kfree(kbuf);
					return result;
				}
		 	}

			/* Remove the procnode from the current process's
			 * p_children */
			procnode_list_remove(curproc->p_children, childnode);
			kfree(kbuf);			

			/* Free the child's pid so it can be used again */
			result = free_pid(pid);
			if (result) {
				return result;				
			}

			/* Return the child's pid */
			*retval = pid;
			return 0;

		} else {
			/* If we reached this point in sys_waitpid, then the
			 * child process has not yet exited.  So the parent waits on the child. */
			P(childnode->waitsem);

			/* When the child exits, it will set the exitcode field
			 * of the procnode.  Therefore, the parent can obtain the exitcode by reading
			 * the exitcode field of the procnode.*/
			*kbuf = childnode->exitcode;

			/* Copy out the exitcode to the status buffer  */
			if (status != NULL) {
				result = copyout(kbuf, status, sizeof(*kbuf));
				if (result) {
					kfree(kbuf);
					return result;
				}
		 	}

			/* Remove the procnode from the current process's
			 * p_children */
			procnode_list_remove(curproc->p_children, childnode);
			kfree(kbuf);

			/* Free the child's pid so it can be used again */
			result = free_pid(pid);		
			if (result) {
				return result;
			}
	
			/* Return the child's pid */
			*retval = pid;
			return 0;
		} 
	}
}
/*
 * sys__exit
 *
 * Terminate the process
 */
void sys__exit(int exitcode) {
	int i;
	int retval;
	int result;
	struct procnode *procnode;
	struct proc *cur_p;
	struct thread *cur_t;

	/* Obtain the procnode the current process shares with its parent */
	procnode = curproc->p_parent;

	if (procnode != NULL) {
		KASSERT(procnode->pid == curproc->p_pid);
	
		if (procnode->pn_refcount > 1) {
			/* If procnode->pn_refcount is greater than 1, the
			 * parent process is still active.  */
			lock_acquire(procnode->pn_lock);
			if (procnode->pn_refcount == 1) {
				/* While acquring the lock, the parent process
				 * exited.  There is no need to send the
				 * exitcode to the parent. */
				lock_release(procnode->pn_lock);
				/* Free the pid so it can be used again */
				result = free_pid(procnode->pid);
				if (result) {
					procnode_destroy(procnode);
					panic("free_pid in sys__exit failed\n");
				}
				/* Destroy the procnode */
				procnode_destroy(procnode);
			} else {
				/* After the lock is acquired, the parent
				 * process is still active.  The child needs to
				 * send the exitcode */

				/* Create the exitcode */
				procnode->exitcode = _MKWAIT_EXIT(exitcode);

				/* Decrement the refcount in the procnode */				
				procnode->pn_refcount--;

				/* Release the lock on the procnode and
				 * increment the procnode semaphore.  */	
				lock_release(procnode->pn_lock);
				V(procnode->waitsem);
			}			
		} else {
			/* If this point in sys__exit has been reached, the
			 * parent has already exited.  There is no need to send
			 * the exitcode to the parent. */

			 /* Free the pid so it can be used again */
			result = free_pid(procnode->pid);
			if (result) {
				procnode_destroy(procnode);
				panic("free_pid in sys__exit failed\n");
			}

			/* Destroy the procnode */
			procnode_destroy(procnode);
		}

	} else if (curproc->p_pid >= PID_MIN) {
		/* If this point in sys__exit has been reached, the process has
		 * no parent so there is no need to send any exit codes. We
		 * simply free the pid. */
		result = free_pid(curproc->p_pid);
		if (result) {
			panic("free_pid in sys__exit failed\n");
		}
	}

	/* Before exiting, the current process closes all file table entries */
        for (i=0; i<=curproc->p_filetable->last_fd; i++) {
                if (curproc->p_filetable->entries[i] != NULL) {
                        sys_close(i, &retval);
                }
        }

	/* Remove the thread from the current process and destroy the process */
	cur_p = curproc;
	cur_t = curthread;
	proc_remthread(cur_t);
	proc_destroy(cur_p);	
        thread_exit();	
}

int
sys_sbrk(intptr_t amount, void *retval)  {
	int npages;
	int result;
	vaddr_t hbase;
	vaddr_t old_htop;
	vaddr_t new_htop;
	vaddr_t htop_limit;
	vaddr_t h_ptr;
	paddr_t paddr;
	struct tlbshootdown *ts;
	struct addrspace *as;
	unsigned sw_offset;

	as = proc_getas();

	old_htop = as->as_heaptop;
	hbase = as->as_vbase2 + as->as_npages2 * PAGE_SIZE;
	new_htop = as->as_heaptop + amount;
	htop_limit = hbase + MAX_HEAPSIZE * PAGE_SIZE;
	npages = (int)(((new_htop & PAGE_FRAME) - hbase)/PAGE_SIZE);
	if (new_htop & ~PAGE_FRAME) {
		npages++;
	}
	
	if (new_htop < hbase) {
		*(vaddr_t *)retval = -1;
		return EINVAL;

	} else if (new_htop <= as->as_stackptr && new_htop < htop_limit) {

		if (amount == 0) {
			*(vaddr_t *)retval = old_htop;
			return 0;

		} else if (amount > 0) {

			if (as->as_heappgtable == NULL) {
				as->as_heappgtable =
				kmalloc(MIN_HEAPSZ*sizeof(int));
				if (as->as_heappgtable == NULL) {
					*(vaddr_t *)retval = -1;
					return ENOMEM;
				}
			}

			while (npages > as->as_heapsz) {
				result = as_growheap(as);
				if (result) {
					*(vaddr_t *)retval = -1;
					return result;
				}		
			}

		} else {

			h_ptr = new_htop;
			
			while (h_ptr < old_htop) {
				lock_acquire(as->as_lock);
				int index = (int)((h_ptr - hbase)/PAGE_SIZE);
				as->as_heappgtable[index] |= PG_BUSY;
				lock_release(as->as_lock);

				if (as->as_heappgtable[index] & PG_VALID) {
					ts = kmalloc(sizeof (const struct
					tlbshootdown));
					if (ts == NULL) {
						as->as_heappgtable[index] &=
						~PG_BUSY;
						*(vaddr_t *)retval = -1;
						return ENOMEM;
					}

					paddr = (as->as_heappgtable[index] & PG_FRAME)
					<< 12;
					ts->ts_paddr = paddr;
					vm_tlbshootdown(ts);
					kfree(ts);
					coremap_freepage(paddr);

				} else if (as->as_heappgtable[index] & PG_SWAP) {
					sw_offset =
					(unsigned)(as->as_heappgtable[index] &
					PG_FRAME);
					lock_acquire(kswap->sw_disklock);
					bitmap_unmark(kswap->sw_diskoffset,
					sw_offset);
					lock_release(kswap->sw_disklock);

				} else {
					KASSERT(as->as_heappgtable[index] ==
					PG_BUSY);
				}

				as->as_heappgtable[index] = 0;
			
				h_ptr += PAGE_SIZE;	
			}
			
			while (npages <  as->as_heapsz/2) {

				if (as->as_heapsz == MIN_HEAPSZ) {
					break;
				}

				result = as_shrinkheap(as);
				if (result) {
					*(vaddr_t *)retval = -1;
					return result;
				}			
			}					

		}
	
		*(vaddr_t *)retval = old_htop;
		as->as_heaptop = new_htop;
		return 0;

	} else {
		*(vaddr_t *)retval = -1;
		return ENOMEM;
	}	
}
