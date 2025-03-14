# ops-class

OS161: Operating System Design and Implementation

[ops-class.org](https://ops-class.org/)

Implemented in C

* ASST1: Synchronization primitive implementation
  * spinlock
  * sleep lock
  * condition variable
  * reader-writer locks
* ASST2: System Call implementation [[test](ASST2)]
  * traps, trap frames
  * file system calls: open, read, write, lseek, close, dup2, chdir
  * process system calls: fork, execv, getpid, waitpid, exit
* ASST3: Virtual Memory implementation [[test](ASST3)]
  * sbrk
  * Software managed TLB
  * Coremap
  * Address spaces
  * Page tables, page fault handling
  * Page swapping

Install notes
* sys161 will run on Ubuntu 16.04 LTS Xenial.
* Create Xenial instance on VirtualBox.
* Install os161 tools on Xenial instance.