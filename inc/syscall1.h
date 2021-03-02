#ifndef INC_SYSCALL1_H_
#define INC_SYSCALL1_H_

#include <stdint.h>

#include "trap.h"
#include "types.h"

#define NELEM(x) (sizeof(x) / sizeof((x)[0]))
#define MAXARG   32

// kern/syscall.c

int fetchint(uint64_t, int64_t*);
int fetchstr(uint64_t, char**);
int argint(int, uint64_t*);
int argptr(int, char**, int);
int argstr(int, char**);

// kern/syscall1.c

int sys_gettid();
int sys_ioctl();
int sys_rt_sigprocmask();

// kern/sysproc.c

int sys_exec();
int sys_yield();
size_t sys_brk();
int sys_clone();
int sys_wait4();
int sys_exit();

// kern/sysfile.c

int sys_dup();
ssize_t sys_read();
ssize_t sys_write();
ssize_t sys_writev();
int sys_close();
int sys_fstat();
int sys_fstatat();
int sys_openat();
int sys_mkdirat();
int sys_mknodat();
int sys_chdir();

// kern/exec.c

int execve(const char*, char* const*, char* const*);

int syscall1(struct trapframe*);

#endif  // INC_SYSCALL1_H_
