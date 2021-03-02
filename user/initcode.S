# Initial process execs /init.

#include <syscall.h>

.globl start

# exec(init, argv)
start:
    ldr     x0, =init
    ldr     x1, =argv
    mov     x8, #SYS_execve
    svc     0x00

exit:
    mov     x8, #SYS_exit
    svc     0x00
    b       exit

# char init[] = "/init\0";
init:
    .string "/init\0"

# char *argv[] = { init, 0 };
.p2align 4
argv:
    .word init
    .word 0
    .word 0
    .word 0
