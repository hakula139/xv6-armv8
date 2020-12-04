#ifndef INC_PROC_H_
#define INC_PROC_H_

#include <stdint.h>

#include "arm.h"
#include "trap.h"

#define NCPU       4    /* maximum number of CPUs */
#define NPROC      64   /* maximum number of processes */
#define KSTACKSIZE 4096 /* size of per-process kernel stack */

#define thiscpu (&cpus[cpuid()])

struct cpu {
    struct context* scheduler; /* swtch() here to enter scheduler */
    struct proc* proc;         /* The process running on this cpu or null */
};

extern struct cpu cpus[NCPU];

// Saved registers for kernel context switches.
struct context {
    // Callee-saved Registers
    uint64_t x19;
    uint64_t x20;
    uint64_t x21;
    uint64_t x22;
    uint64_t x23;
    uint64_t x24;
    uint64_t x25;
    uint64_t x26;
    uint64_t x27;
    uint64_t x28;
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct proc {
    uint64_t sz;             /* Size of process memory (bytes)          */
    uint64_t* pgdir;         /* Page table                              */
    char* kstack;            /* Bottom of kernel stack for this process */
    enum procstate state;    /* Process state                           */
    int pid;                 /* Process ID                              */
    struct proc* parent;     /* Parent process                          */
    struct trapframe* tf;    /* Trapframe for current syscall           */
    struct context* context; /* swtch() here to run process             */
    void* chan;              /* If non-zero, sleeping on chan           */
    int killed;              /* If non-zero, have been killed           */
    char name[16];           /* Process name (debugging)                */
};

void proc_init();
void user_init();
void scheduler();

void exit();

#endif  // INC_PROC_H_
