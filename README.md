# Operating Systems — Linux Kernel Development

A semester-long kernel development project built as part of CSE330 
(Operating Systems) at Arizona State University. Starting from compiling 
a custom Linux kernel, this project progressively implements core OS 
components from scratch in C, directly inside the Linux kernel.

## What This Project Covers

**Custom Kernel Compilation**
Compiled and installed a custom Linux 6.10 kernel from source on Ubuntu 
24.04, configured using menuconfig, and booted via GRUB on a VirtualBox VM.

**Kernel Modules & System Calls**
Built a loadable kernel module with runtime parameters and added a brand 
new system call to the Linux kernel's system call table, tested with a 
custom userspace program.

**Thread Synchronization — Producer Consumer Problem**
Implemented a kernel module that spawns configurable producer and consumer 
kernel threads, synchronized using semaphores to safely manage a shared 
resource without deadlocks.

**Process Memory Allocation**
Built a kernel module that handles `mmap`-style memory allocation requests 
from userspace via `ioctl`. Walks and modifies the Linux 5-level page table 
(PGD → P4D → PUD → PMD → PTE) to map physical pages into a process's 
address space, with allocation limits enforced at the kernel level.

**USB Block Device Access**
Wrote a kernel module that exposes direct block-level read and write access 
to a virtual USB storage device from userspace using Linux's Block I/O (BIO) 
abstraction, supporting variable operation sizes and offsets.

## Tech Stack

![C](https://img.shields.io/badge/C-A8B9CC?style=flat&logo=c&logoColor=black)
![Linux](https://img.shields.io/badge/Linux_Kernel_6.10-FCC624?style=flat&logo=linux&logoColor=black)
![Ubuntu](https://img.shields.io/badge/Ubuntu_24.04-E95420?style=flat&logo=ubuntu&logoColor=white)
![GCC](https://img.shields.io/badge/GCC-A8B9CC?style=flat&logo=gnu&logoColor=black)

## Key Concepts

- Kernel compilation, configuration, and GRUB bootloader setup
- Loadable kernel modules (`insmod`, `rmmod`, `printk`, `module_param`)
- Adding system calls to the Linux kernel syscall table
- Kernel threads (`kthread_run`) and semaphore synchronization
- Virtual devices and `ioctl` for userspace-kernel communication
- 5-level page table traversal and physical memory mapping
- Linux Block I/O (`struct bio`, `bio_alloc`, `submit_bio_wait`)

## Example Usage

```bash
# Load the producer-consumer kernel module
sudo insmod producer_consumer.ko prod=2 cons=2 size=5
dmesg | tail -20
sudo rmmod producer_consumer

# Run the block device test
./test.sh read 512 1024 0
```

## What I Learned

- How to build and boot a custom Linux kernel from source
- How the kernel exposes functionality through modules and system calls
- How thread synchronization works at the kernel level using semaphores
- How operating systems manage process memory through multi-level page tables
- How the Linux block I/O layer abstracts access to storage devices
