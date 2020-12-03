#ifndef INC_SYSCALL_H_
#define INC_SYSCALL_H_

#include <stdint.h>

#include "syscallno.h"

int argstr(int, char**);
int argint(int, uint64_t*);
int fetchstr(uint64_t, char**);

int syscall();

#endif  // INC_SYSCALL_H_