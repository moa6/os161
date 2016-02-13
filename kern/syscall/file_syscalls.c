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
#include <limits.h>
#include <copyinout.h>
#include <syscall.h>
#include <current.h>
#include <proc.h>
#include <stat.h>
#include <device.h>
#include <kern/errno.h>
#include <kern/seek.h>
#include <kern/stat.h>
#include <kern/fcntl.h>
#include <uio.h>
#include <filetable.h>

/*
 * sys_open
 * 
 * sys_open opens the file, device, or other kernel object named by filename.
 * The flags argument specifies how to open the file.  The optional mode
 * argument provides the file permissions to use.
 */
int
sys_open(const_userptr_t filename, int flags, mode_t mode, int* retval)
{
	int fd;
	int result;
	int last_fd;
	size_t len;
	struct vnode *v;

	/* Set the return value to -1 */
	*retval = -1;
	/* Retrieve the last_fd which represents the largest file descriptor
 * being used */
	last_fd = curproc->p_filetable->last_fd;

	char *k_filename = kmalloc(PATH_MAX*sizeof(char));
	if (k_filename == NULL) {
		return ENOMEM;
	}
	
	/* Copy filename from user space to the kernel */
	result = copyinstr(filename, k_filename, PATH_MAX, &len);
	if (result) {
		kfree(k_filename);
		return result;
	}

	/* Call vfs_open which does most of the work of the open system call */
	result = vfs_open(k_filename, flags, mode, &v);
	if (result) {
		kfree(k_filename);
		return result;
	}

	/* Go through the file table to find the smallest available file
 * descriptor */
	for(fd=0; fd<curproc->p_filetable->filetable_size; fd++) {
		if (curproc->p_filetable->entries[fd] == NULL) {
			break; 
		}
	}

	if (fd == OPEN_MAX) {
		/* Return EMFILE if the process already has the maximum number
 * of files open */
		kfree(k_filename);
		return EMFILE;
	} else if (fd == curproc->p_filetable->filetable_size) {
		/* Grow file table if the file table is full but has not yet
 * reached maximum capacity */
		filetable_grow(curproc->p_filetable);
	}
	/* Create a new file table entry */
	curproc->p_filetable->entries[fd] = file_entry_create();
	curproc->p_filetable->entries[fd]->vn = v;
	curproc->p_filetable->entries[fd]->openflags = flags & O_ACCMODE;
	curproc->p_filetable->entries[fd]->seek = 0;
	curproc->p_filetable->entries[fd]->f_refcount++;

	/* Set the last_fd if necessary */
	if (fd > last_fd) {
		curproc->p_filetable->last_fd = fd;
	}

	/* Set the return value to 0 and return 0 */
	kfree(k_filename);
	*retval = fd;
	return 0;
}


/*
 * sys_close
 *
 * sys_close closes file handle fd.
 */
int
sys_close(int fd, int* retval)
{
        int i;
        int last_fd;

	/* Set the return value to -1 */
        *retval = -1;
	/* Retrieve last_fd which represents the largest file descriptor being
 * used */
        last_fd = curproc->p_filetable->last_fd;

        if (fd > last_fd || curproc->p_filetable->entries[fd] == NULL || fd < 0)
{
		/* Return EBADF if fd is not a valid file descriptor */
                return EBADF;
        } else if (curproc->p_filetable->entries[fd]->f_refcount > 1) {
		/* If more than one file descriptor is pointing to the file
 * entry, then decrement f_refcount and set fd pointing to NULL */
                curproc->p_filetable->entries[fd]->f_refcount--;
                curproc->p_filetable->entries[fd] = NULL;
        } else {
		/* Call vfs_close which does most of the work of the close
 * system call */
                vfs_close(curproc->p_filetable->entries[fd]->vn);
		/* Destroy the file table entry and set fd pointing to NULL */ 
                file_entry_destroy(curproc->p_filetable->entries[fd]);
                curproc->p_filetable->entries[fd] = NULL;
        }

	/* Determine if the file table needs to be re-sized */
        if (fd == last_fd) {
                 last_fd--;
		 for (i = last_fd; i > 0; i--) {
			if (curproc->p_filetable->entries[i] != NULL) {
				last_fd = i;
				break;
			}
		}
        }
        if (last_fd < 8) {
		while (curproc->p_filetable->filetable_size > 8) {
			filetable_shrink(curproc->p_filetable);
		}
	} else if (last_fd < 16) {
		while (curproc->p_filetable->filetable_size > 16) {
			filetable_shrink(curproc->p_filetable);
		}
	}

	/* Set the return value to 0 and return 0 */
        *retval = 0;
        return 0;
}

/*
 * sys_write
 *
 * sys_write writes up to buflen bytes to the file specified by fd, at the
 * location in the file specified by the current seek position of the file,
 * taking data from the space pointed to by buf.  The file must be open for
 * writing.
 */
int
sys_write(int fd, void *buf, size_t buflen, int* retval)
{
	int last_fd;
	int openflags;
	int result;
	off_t pos;
	off_t bytes_write;
	struct uio ku;
	struct iovec iov;
	struct vnode *vn;
	void* kbuf;

	/* Set the return value to -1 */
	*retval = -1;
	/* Retrieve last_fd which represents the largest available file
 * descriptor */
	last_fd = curproc->p_filetable->last_fd;
	
	if (fd > last_fd || curproc->p_filetable->entries[fd] == NULL || fd < 0) {
		/* Return EBADF if fd is not a valid file descriptor */
		return EBADF;
	} else if (buf == NULL) {
		/* Return EFAULT if buf is not valid */
		return EFAULT;
	} else {
		/* Check the openflags of the file.  Return EBADF if the there
 * are no write permissions to the file */
		openflags = curproc->p_filetable->entries[fd]->openflags;
		if (openflags == O_RDONLY) {
			return EBADF;
		}

		/* Obtain the vnode and seek position of the file */
		vn = curproc->p_filetable->entries[fd]->vn;
		pos = curproc->p_filetable->entries[fd]->seek;

		/* Create a kernel buffer */
		kbuf = kmalloc(buflen*sizeof(void));
		if (kbuf == NULL) {
			return ENOMEM;
		}

		/* Copy data from buf in user space to the kernel buffer */
		result = copyin((const_userptr_t)buf, kbuf, buflen);
		if (result) {
			kfree(kbuf);
			return result;
		}

		/* Initialize uio and perform the write */
		uio_kinit(&iov, &ku, (void*)kbuf, buflen, pos, UIO_WRITE);
		result = VOP_WRITE(vn, &ku);
		if (result) {
			kfree(kbuf);
			return result;
		}

		/* Calculate the number of bytes written */
		bytes_write = ku.uio_offset - pos;
		/* Update the seek position */
		curproc->p_filetable->entries[fd]->seek = ku.uio_offset;
		/* Free the kernel buffer */
		kfree(kbuf);
		/* Set the return value to the number of bytes written and
 * return 0 */
		*retval = (int)bytes_write;
		return 0;
	}
}

/*
 * sys_read
 *
 * sys_read reads up to buflen bytes from the file specified by fd, at the
 * location in the file specified by the current seek position of the file, and
 * stores them in the space pointed to by buf.  The file must be open for
 * reading.
 */
int
sys_read(int fd, void *buf, size_t buflen, int* retval)
{
	int last_fd;
	int openflags;
	int result;
	off_t pos;
	off_t bytes_read;
	struct uio ku;
	struct iovec iov;
	struct vnode *vn;
	void* kbuf;

	/* Set the return value to -1 */
	*retval = -1;
	/* Retrieve last_fd which is the largest used file descriptor */
	last_fd = curproc->p_filetable->last_fd;
	
	if (fd > last_fd || curproc->p_filetable->entries[fd] == NULL || fd < 0) {
		/* Return EBADF if fd is not a valid file descriptor */
		return EBADF;
	} else if (buf == NULL) {
		/* Return EFAULT if buf is not valid */
		return EFAULT;
	} else {
		/* Check the open flags of the file.  Return EBADF if there are
 * no read permissions to the file */
		openflags = curproc->p_filetable->entries[fd]->openflags;
		if (openflags == O_WRONLY) {
			return EBADF;
		}

		/* Obtain the vnode and seek position of the file */
		vn = curproc->p_filetable->entries[fd]->vn;
		pos = curproc->p_filetable->entries[fd]->seek;
		
		/* Create a kernel buffer */
		kbuf = kmalloc(buflen*sizeof(void));
		if (kbuf == NULL) {
			return ENOMEM;
		}		

		/* Initialize a uio and perform the read */
		uio_kinit(&iov, &ku, kbuf, buflen, pos, UIO_READ);
		result = VOP_READ(vn, &ku);
		if (result) {
			kfree(buf);
			return result;
		}

		/* Copy the data from the kernel buffer to buf in user space */
		result = copyout(kbuf, (userptr_t)buf, buflen);
		if (result) {
			kfree(kbuf);
			return result;
		}

		/* Calculate the number of bytes read */
		bytes_read = ku.uio_offset - pos;
		/* Update the seek position */
		curproc->p_filetable->entries[fd]->seek = ku.uio_offset;
		/* Free the kernel buffer */
		kfree(kbuf);

		/* Set the return value to the number of bytes read and return 0 */
		*retval = (int)bytes_read;
		return 0;
	}
}

/*
 * sys_lseek
 *
 * sys_lseek alters the current seek position of the file, seeking a new
 * position based on pos and whence.
 */
int
sys_lseek(int fd, off_t pos, int whence, int* retval, int* retval_v1) 
{
	int last_fd;
	int result;
	off_t old_seek;
	off_t new_seek;
	struct stat file_stat;

	/* Initialize the return value which is 64 bits */
	*retval = -1;
	*retval_v1 = 0;

	/* Obtain last_fd which is the largest used file descriptor */
	last_fd = curproc->p_filetable->last_fd;

	if (fd > last_fd || curproc->p_filetable->entries[fd] == NULL || fd < 0) {
		/* Return EBADF if fd is not a valid file descriptor */
		return EBADF;
	} else { 
		old_seek = curproc->p_filetable->entries[fd]->seek;
		result = VOP_STAT(curproc->p_filetable->entries[fd]->vn,
&file_stat);
		if (result) {
			return result;
		}

		switch(whence) {
		    case SEEK_SET:
			/* If SEEK_SET, the new seek position is pos */
			new_seek = pos;
			break;

		    case SEEK_CUR:
			/* If SEEK_CUR, the new seek position is the current
 position plus pos */
			new_seek = old_seek + pos;
			break;

		    case SEEK_END:
			/* If SEEK_END, the new seek position is the position at
 * EOF plus pos */
			new_seek = file_stat.st_size + pos;
			break;

		    default:
			/* Return EINVAL if whence is invalid */
			return EINVAL;
		}

		if ((file_stat.st_mode & S_IFCHR) == S_IFCHR) {
			/* Return ESPIPE if file pointed to by fd does not
 * support seeking */
			return ESPIPE;
		} else if (new_seek < 0) {
			/* Return EINVAL if the new seek position ends up being
 * less than zero */
			return EINVAL;
		} 

		/* Update the seek position */
		curproc->p_filetable->entries[fd]->seek = new_seek;
		/* Set the return value to the new seek position and return 0 */
		*retval = (int)(new_seek >> 32);
		*retval_v1 = (int)((new_seek << 32) >> 32);
		return 0;
	}
}

/*
 * sys_dup2
 *
 * sys_dup2 clones the file handle oldfd onto the new file handle newfd.  If
 * newfd is already an open file, that file is closed. 
 */
int
sys_dup2 (int oldfd, int newfd, int* retval) {
	int last_fd;
	
	/* Set the return value to -1 */
	*retval = -1;
	/* Retrieve last_fd which is the largest used file descriptor */
	last_fd = curproc->p_filetable->last_fd;

	if (oldfd > last_fd || curproc->p_filetable->entries[oldfd] == NULL ||
oldfd < 0) {
		/* Return EBADF if oldfd is an invalid file descriptor */
		return EBADF;
	} else if (newfd >= OPEN_MAX || newfd < 0) {
		/* Return EBADF if newfd is an invalid file descriptor */
		return EBADF;
	} else if (oldfd == newfd) {
		/* If oldfd equals newfd, set the return value to newfd and
 * return 0 */
		*retval = newfd;
		return 0;
	} else {
		if (curproc->p_filetable->entries[newfd] != NULL) {
			/* If newfd still points to an open file, close that
 * file */
			sys_close(newfd, retval);
			*retval = -1;
		}

		/* Set oldfd and newfd to point to the same file entry */
		curproc->p_filetable->entries[newfd] =
curproc->p_filetable->entries[oldfd];
		curproc->p_filetable->entries[newfd]->f_refcount++;
	
		/* Updated last_fd is necessary */
		if (newfd > last_fd) {
			curproc->p_filetable->last_fd = newfd;
		}

		/* Set the return value to newfd and return 0 */
		*retval = newfd;
		return 0;
	}
}

/*
 * sys__getcwd
 *
 * sys__getcwd returns the name of the current directory.
 */
int
sys___getcwd(void* buf, size_t buflen, int* retval) 
{
	int result;
	struct iovec iov;
	struct uio ku;
	void* kbuf;

	/* Set the return value to -1 */
	*retval = -1;

	if (buf == NULL) {
		/* Return EFAULT if buf points to NULL */
		return EFAULT;
	} else {
		/* Create a kernel buffer */
		kbuf = kmalloc(buflen*sizeof(void));
		if (kbuf == NULL) {
			return ENOMEM;
		}

		/* Initialize a uio and retrieve the name of the current
 * directory */
		uio_kinit(&iov, &ku, kbuf, buflen, 0, UIO_READ);
		result = vfs_getcwd(&ku);
		if (result) {
			kfree(kbuf);
			return result;
		}

		/* Copy the name of the current directory from the kernel buffer
 * to buf in user space */
		result = copyout(kbuf, (userptr_t)buf, buflen);
		if (result) {
			kfree(kbuf);
			return result;
		}

		/* Free the kernel buffer */
		kfree(kbuf);
		/* Set the return value to length of data retrieved and return 0
 * */
		*retval = ku.uio_offset;
		return 0;
	}
}

/*
 * sys_chdir
 *
 * sys_chdir sets the current directory of the process to the directory
 * specified by pathname.
 */
int
sys_chdir(const_userptr_t pathname, int* retval) 
{
	int result;
	size_t len;

	/* Set the return value to -1 */
	*retval = -1;

	/* Create a kernel buffer */
        char *k_path = kmalloc(PATH_MAX*sizeof(char));
        if (k_path == NULL) {
                return ENOMEM;
        }
	/* Copy the pathname into the kernel buffer */
        result = copyinstr(pathname, k_path, PATH_MAX, &len);
	if (result) {
		kfree(k_path);
		return result;
	}

	/* Change the current directory */
	result = vfs_chdir(k_path);
	if (result) {
		kfree(k_path);
		return result;
	}

	/* Free the kernel buffer */
	kfree(k_path);
	/* Set the return value to 0 and return 0 */
	*retval = 0;
	return 0;
}
