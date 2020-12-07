#include "console.h"
#include "proc.h"
#include "syscall.h"

int
sys_exit()
{
    cprintf("sys_exit: in exit\n");
    exit(0);
    return 0;
}
