#include "proc.h"

#include "arm.h"
#include "console.h"
#include "kalloc.h"
#include "mmu.h"
#include "spinlock.h"
#include "string.h"
#include "trap.h"
#include "vm.h"

struct cpu cpus[NCPU];

struct {
    struct proc proc[NPROC];
} ptable;

static struct proc* initproc;

int nextpid = 1;
struct spinlock pid_lock;

struct spinlock wait_lock;

void forkret();
extern void trapret();
void swtch(struct context**, struct context*);

int
pid_next()
{
    acquire(&pid_lock);
    int pid = nextpid++;
    release(&pid_lock);
    return pid;
}

/*
 * Initialize the spinlock for ptable to serialize the access to ptable
 */
void
proc_init()
{
    initlock(&wait_lock, "wait_lock");
    initlock(&pid_lock, "pid_lock");
    for (struct proc* p = ptable.proc; p < &ptable.proc[NPROC]; ++p) {
        initlock(&p->lock, "proc_lock");
    }
    cprintf("proc_init: success.\n");
}

/*
 * Free a proc structure and the data hanging from it,
 * including user pages.
 * p->lock must be held.
 */
static void
proc_free(struct proc* p)
{
    p->chan = NULL;
    p->killed = 0;
    p->xstate = 0;
    p->pid = 0;
    p->parent = NULL;
    if (p->kstack) kfree(p->kstack);
    p->kstack = NULL;
    p->sz = 0;
    if (p->pgdir) kfree((char*)p->pgdir);
    p->pgdir = NULL;
    if (p->tf) kfree((char*)p->tf);
    p->tf = NULL;
    p->name[0] = '\0';
    p->state = UNUSED;
}

/*
 * Look through the process table for an UNUSED proc.
 * If found, change state to EMBRYO and initialize
 * state required to run in the kernel.
 * Otherwise return 0.
 */
static struct proc*
proc_alloc()
{
    for (struct proc* p = ptable.proc; p < &ptable.proc[NPROC]; ++p) {
        acquire(&p->lock);
        if (p->state != UNUSED) {
            release(&p->lock);
            continue;
        }

        p->pid = pid_next();

        // Allocate kernel stack.
        if (!(p->kstack = kalloc())) {
            proc_free(p);
            release(&p->lock);
            return NULL;
        }
        char* sp = p->kstack + KSTACKSIZE;

        // Allocate a trapframe page.
        if (!(p->tf = (struct trapframe*)kalloc())) {
            proc_free(p);
            release(&p->lock);
            return NULL;
        }

        // Set up new context to start executing at forkret.
        sp -= sizeof(*p->context);
        p->context = (struct context*)sp;
        memset(p->context, 0, sizeof(*p->context));
        p->context->x30 = (uint64_t)forkret;

        p->state = EMBRYO;
        cprintf("proc_alloc: proc %d success.\n", p->pid);
        return p;
    }
    return NULL;
}

/*
 * Set up first user process (only used once).
 * Set trapframe for the new process to run
 * from the beginning of the user process determined
 * by uvm_init
 */
void
user_init()
{
    extern char _binary_obj_user_initcode_start[];
    extern char _binary_obj_user_initcode_size[];

    struct proc* p = proc_alloc();
    if (!p) panic("user_init: process failed to allocate.\n");
    initproc = p;

    // Allocate a user page table.
    if (!(p->pgdir = pgdir_init()))
        panic("user_init: page table failed to allocate.\n");
    p->sz = PGSIZE;

    // Copy initcode into the page table.
    uvm_init(
        p->pgdir, _binary_obj_user_initcode_start,
        (uint64_t)_binary_obj_user_initcode_size);

    // Set up trapframe to prepare for the first "return" from kernel to user.
    memset(p->tf, 0, sizeof(*p->tf));
    p->tf->x30 = 0;          // initcode start address
    p->tf->sp_el0 = PGSIZE;  // user stack pointer

    strncpy(p->name, "initproc", sizeof(p->name));
    p->state = RUNNABLE;
    release(&p->lock);

    cprintf("user_init: proc %d (%s) success.\n", p->pid, p->name, cpuid());
}

/*
 * Per-CPU process scheduler
 * Each CPU calls scheduler() after setting itself up.
 * Scheduler never returns. It loops, doing:
 *  - choose a process to run
 *  - swtch to start running that process
 *  - eventually that process transfers control
 *    via swtch back to the scheduler.
 */
void
scheduler()
{
    struct cpu* c = thiscpu;
    c->proc = NULL;

    while (1) {
        // Enable interrupts on this processor to avoid deadlock.
        sti();

        // Loop over process table looking for process to run.
        for (struct proc* p = ptable.proc; p < &ptable.proc[NPROC]; ++p) {
            acquire(&p->lock);
            if (p->state != RUNNABLE) {
                release(&p->lock);
                continue;
            }

            // Switch to chosen process. It is the process's job
            // to release its lock and then reacquire it
            // before jumping back to us.
            c->proc = p;
            uvm_switch(p);
            p->state = RUNNING;
            cprintf(
                "scheduler: switch to proc %d at CPU %d.\n", p->pid, cpuid());

            swtch(&c->scheduler, p->context);

            // Process is done running for now.
            // It should have changed its p->state before coming back.
            c->proc = NULL;
            release(&p->lock);
        }
    }
}

/*
 * Enter scheduler. Must hold only p->lock
 * and have changed p->state.
 */
void
sched()
{
    struct cpu* c = thiscpu;
    struct proc* p = c->proc;

    if (!holding(&p->lock)) panic("sched: process not locked.\n");
    if (p->state == RUNNING) panic("sched: process running.\n");

    swtch(&p->context, c->scheduler);
}

/*
 * A fork child's very first scheduling by scheduler()
 * will swtch to forkret. "Return" to user space.
 */
void
forkret()
{
    struct proc* p = thiscpu->proc;

    // Still holding p->lock from scheduler.
    release(&p->lock);
}

/*
 * Pass p's abandoned children to initproc.
 * Caller must hold wait_lock.
 */
void
reparent(struct proc* p)
{
    for (struct proc* pc = ptable.proc; pc < &ptable.proc[NPROC]; ++pc) {
        if (pc->parent == p) { pc->parent = initproc; }
    }
}

/*
 * Exit the current process. Does not return.
 * An exited process remains in the zombie state
 * until its parent calls wait() to find out it exited.
 */
void
exit(int status)
{
    struct proc* p = thiscpu->proc;
    if (p == initproc) panic("exit: initproc exiting.\n");

    acquire(&wait_lock);

    // Give any children to init.
    reparent(p);

    acquire(&p->lock);
    p->xstate = status;
    p->state = ZOMBIE;

    release(&wait_lock);

    // Jump into the scheduler, never return.
    sched();

    panic("exit: zombie returned!\n");
}
