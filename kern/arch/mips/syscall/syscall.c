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
#include <kern/errno.h>
#include <kern/syscall.h>
#include <lib.h>
#include <mips/trapframe.h>
#include <thread.h>
#include <copyinout.h>
#include <current.h>
#include <syscall.h>

/*
 * System call dispatcher.
 *
 * A pointer to the trapframe created during exception entry (in
 * exception-*.S) is passed in.
 *
 * The calling conventions for syscalls are as follows: Like ordinary
 * function calls, the first 4 32-bit arguments are passed in the 4
 * argument registers a0-a3. 64-bit arguments are passed in *aligned*
 * pairs of registers, that is, either a0/a1 or a2/a3. This means that
 * if the first argument is 32-bit and the second is 64-bit, a1 is
 * unused.
 *
 * This much is the same as the calling conventions for ordinary
 * function calls. In addition, the system call number is passed in
 * the v0 register.
 *
 * On successful return, the return value is passed back in the v0
 * register, or v0 and v1 if 64-bit. This is also like an ordinary
 * function call, and additionally the a3 register is also set to 0 to
 * indicate success.
 *
 * On an error return, the error code is passed back in the v0
 * register, and the a3 register is set to 1 to indicate failure.
 * (Userlevel code takes care of storing the error code in errno and
 * returning the value -1 from the actual userlevel syscall function.
 * See src/user/lib/libc/arch/mips/syscalls-mips.S and related files.)
 *
 * Upon syscall return the program counter stored in the trapframe
 * must be incremented by one instruction; otherwise the exception
 * return code will restart the "syscall" instruction and the system
 * call will repeat forever.
 *
 * If you run out of registers (which happens quickly with 64-bit
 * values) further arguments must be fetched from the user-level
 * stack, starting at sp+16 to skip over the slots for the
 * registerized values, with copyin().
 */
void
syscall(struct trapframe *tf)
{
	int callno;
	int32_t retval;
	size_t return_size;
	int fd, newfd;
	int err;
	off_t pos;
	userptr_t whence_ptr;
	int whence;
	off_t abs_offset;
	char buf[sizeof(int)];
	pid_t pid;

	KASSERT(curthread != NULL);
	KASSERT(curthread->t_curspl == 0);
	KASSERT(curthread->t_iplhigh_count == 0);

	callno = tf->tf_v0;

	/*
	 * Initialize retval to 0. Many of the system calls don't
	 * really return a value, just 0 for success and -1 on
	 * error. Since retval is the value returned on success,
	 * initialize it to 0 by default; thus it's not necessary to
	 * deal with it except for calls that return other values,
	 * like write.
	 */

	retval = 0;

	switch (callno) {
		case SYS__exit:
		sys__exit((int)tf->tf_a0);
		panic("syscall: Unexpected return from _exit().");
		break;

		case SYS___getcwd:
		err = sys___getcwd((userptr_t)tf->tf_a0, (size_t)tf->tf_a1, &return_size);
		retval = (int32_t)return_size;
		break;

	    case SYS___time:
		err = sys___time((userptr_t)tf->tf_a0, (userptr_t)tf->tf_a1);
		break;

		case SYS_chdir:
		err = sys_chdir((userptr_t)tf->tf_a0);
		break;
		
		case SYS_close:
		// Close with file descriptor table locking enabled.
		err = sys_close((int)tf->tf_a0, 1);
		break;

		case SYS_dup2:
		newfd = (int)tf->tf_a1;
		err = sys_dup2((int)tf->tf_a0, newfd);
		retval = newfd;
		break;

		case SYS_execv:
		err = sys_execv((userptr_t)tf->tf_a0, (userptr_t)tf->tf_a1);
		break;
		
		case SYS_fork:
		err = sys_fork(&pid, tf);
		// Parent returns here.
		retval = (int32_t)pid;
		break;

		// Hijack getlogin() for debugging utility.
		case SYS___getlogin:
		sys___getlogin();
		err = 0;
		break;

		case SYS_getpid:
		err = sys_getpid(&pid);
		retval = (int32_t)pid;
		break;

		case SYS_lseek:
		// Special handling for passing 64b value with 32b MIPS registers.
		// a0 = fd
		// a1 = unused for alignment
		// a2 = pos (MSB) OS161 MIPS is big endian
		// a3 = pos (LSB)
		// (sp) + 16 (user stack) = whence (MSB)
		pos = (off_t)(((off_t)(tf->tf_a2) << 32) | (tf->tf_a3));
		// Get whence from the user stack.
		whence_ptr = (userptr_t)tf->tf_sp + STACK_OFFSET;
		err = copyin(whence_ptr, buf, sizeof(buf));
		if (err) {
			break;
		}
		whence =  ((int)buf[0] << 24) + 
		          ((int)buf[1] << 16) + 
				  ((int)buf[2] << 8) +
				  (int)buf[3];
		err = sys_lseek((int)tf->tf_a0, pos, whence, &abs_offset);
		// Return 64b abs_offset in big endian format as:
		// v0 = retval = abs_offset (MSB)
		// v1 = abs_offset (LSB)
		retval = (int32_t)(abs_offset >> 32);
		tf->tf_v1 = (int32_t)(abs_offset & 0xffffffff);
		break;

		case SYS_open:
		err = sys_open((const_userptr_t)tf->tf_a0, (int)tf->tf_a1, &fd);
		retval = (int32_t)fd;
		break;

		case SYS_read:
		err = sys_read((int)tf->tf_a0, (userptr_t)tf->tf_a1, (size_t)tf->tf_a2,
		          &return_size);
		retval = (int32_t)return_size;
		break;

	    case SYS_reboot:
		err = sys_reboot(tf->tf_a0);
		break;

		case SYS_waitpid:
		pid = (pid_t)tf->tf_a0;
		err = sys_waitpid(pid, (userptr_t)tf->tf_a1, (int)tf->tf_a2);
		retval = pid;
		break;

	    case SYS_write:
		err = sys_write((int)tf->tf_a0, (userptr_t)tf->tf_a1, (size_t)tf->tf_a2, 
		          &return_size);
		retval = (int32_t)return_size;
		break;

	    default:
		kprintf("Unknown syscall %d\n", callno);
		err = ENOSYS;
		break;
	}


	if (err) {
		/*
		 * Return the error code. This gets converted at
		 * userlevel to a return value of -1 and the error
		 * code in errno.
		 */
		tf->tf_v0 = err;
		tf->tf_a3 = 1;      /* signal an error */
	}
	else {
		/* Success. */
		tf->tf_v0 = retval;
		tf->tf_a3 = 0;      /* signal no error */
	}

	/*
	 * Now, advance the program counter, to avoid restarting
	 * the syscall over and over again.
	 */

	tf->tf_epc += 4;

	/* Make sure the syscall code didn't forget to lower spl */
	KASSERT(curthread->t_curspl == 0);
	/* ...or leak any spinlocks */
	KASSERT(curthread->t_iplhigh_count == 0);
}

/*
 * Enter user mode for a newly forked child process.
 *
 * Installs parent stack image over child stack.
 * Modifies return value to 0 to distinguish child in fork() return.
 * Jumps directly back to usermode via exception return using
 * parent trapframe.
 * 
 * Args:
 *   image: Parent's unmodified stack image.
 */
void
enter_forked_process(void *arg1, unsigned long unused_arg2)
{
	int result;
	struct trapframe *tf_copy;  // Inbound temporary copy.
	struct trapframe *tf;  // This thread's trapframe.

	tf_copy = (struct trapframe *)arg1;
	(void)unused_arg2;
    result = trapframe_load(curthread, &tf, tf_copy);
	kfree(tf_copy);
	if (result) {
		panic("enter_forked_process: Unable to load trapframe.");
	}
	tf->tf_v0 = 0;  // Child returns 0 from fork().
	tf->tf_a3 = 0;  // No error.
	tf->tf_epc += 4;  // Step past originating syscall.
	mips_usermode(tf);	
}

