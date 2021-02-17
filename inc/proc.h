#ifndef INC_PROC_H_
#define INC_PROC_H_

#include <stddef.h>

#include "arm.h"
#include "spinlock.h"
#include "trap.h"

#define NCPU       4    /* maximum number of CPUs */
#define NPROC      64   /* maximum number of processes */
#define NOFILE     16   /* open files per process */
#define KSTACKSIZE 4096 /* size of per-process kernel stack */

#define thiscpu (&cpus[cpuid()])

struct cpu {
    struct context* scheduler; /* swtch() here to enter scheduler */
    struct proc* proc;         /* The process running on this cpu or null */
};

extern struct cpu cpus[];

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

    uint64_t x29;  // Frame Pointer
    uint64_t x30;  // Procedure Link Register
};

enum procstate { UNUSED, EMBRYO, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };

struct proc {
    struct spinlock lock;

    // p->lock must be held when using these:
    enum procstate state;  // Process state
    void* chan;            // If non-zero, sleeping on chan
    int killed;            // If non-zero, have been killed
    int xstate;            // Exit status to be returned to parent's wait
    int pid;               // Process ID

    // wait_lock must be held when using these:
    struct proc* parent;  // Parent process

    // no lock needs to be held when using these:
    char* kstack;                // Bottom of kernel stack for this process
    uint64_t sz;                 // Size of process memory (bytes)
    uint64_t* pgdir;             // Page table
    struct trapframe* tf;        // Trapframe for current syscall
    struct context* context;     // swtch() here to run process
    struct file* ofile[NOFILE];  // Open files
    struct inode* cwd;           // Current directory
    char name[16];               // Process name (debugging)
};

static inline struct proc*
thisproc()
{
    return thiscpu->proc;
}

void proc_init();
void user_init();
void scheduler();
void yield();
void exit(int);
void sleep(void*, struct spinlock*);
int fork();
int wait();
void wakeup(void*);

#endif  // INC_PROC_H_
