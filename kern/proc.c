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
extern void usertrapret(struct trapframe*);
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
    if (p->pgdir) vm_free(p->pgdir, 4);
    p->pgdir = NULL;
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

        // Leave room for trapframe.
        sp -= sizeof(*p->tf);
        p->tf = (struct trapframe*)sp;

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
    if (!p) panic("\tuser_init: process failed to allocate.\n");
    initproc = p;

    // Allocate a user page table.
    if (!(p->pgdir = pgdir_init()))
        panic("\tuser_init: page table failed to allocate.\n");
    p->sz = PGSIZE;

    // Copy initcode into the page table.
    uvm_init(
        p->pgdir, _binary_obj_user_initcode_start,
        (uint64_t)_binary_obj_user_initcode_size);

    // Set up trapframe to prepare for the first "return" from kernel to user.
    memset(p->tf, 0, sizeof(*p->tf));
    p->tf->x30 = 0;          // initcode start address
    p->tf->sp_el0 = PGSIZE;  // user stack pointer
    p->tf->spsr_el1 = 0;     // program status register
    p->tf->elr_el1 = 0;      // exception link register

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

    if (!holding(&p->lock)) panic("\tsched: process not locked.\n");
    if (p->state == RUNNING) panic("\tsched: process running.\n");

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

    // Pass trapframe pointer as an argument when calling trapret.
    usertrapret(p->tf);
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

    // Temporarily disabled before user processes are implemented.
    // if (p == initproc) panic("\texit: initproc exiting.\n");

    acquire(&wait_lock);

    // Give any children to init.
    reparent(p);

    acquire(&p->lock);
    p->xstate = status;
    p->state = ZOMBIE;

    release(&wait_lock);

    // Jump into the scheduler, never return.
    sched();
    panic("\texit: zombie returned!\n");
}

/*
 * Atomically release lock and sleep on chan.
 * Reacquires lock when awakened.
 */
void
sleep(void* chan, struct spinlock* lk)
{
    struct proc* p = thiscpu->proc;

    // Must acquire p->lock in order to
    // change p->state and then call sched.
    // Once we hold p->lock, we can be
    // guaranteed that we won't miss any wakeup
    // (wakeup locks p->lock),
    // so it's okay to release lk.

    acquire(&p->lock);
    release(lk);

    // Go to sleep.
    p->chan = chan;
    p->state = SLEEPING;
    sched();

    // Tidy up.
    p->chan = 0;

    // Reacquire original lock.
    release(&p->lock);
    acquire(lk);
}

/*
 * Wake up all processes sleeping on chan.
 * Must be called without any p->lock.
 */
void
wakeup(void* chan)
{
    for (struct proc* p = ptable.proc; p < &ptable.proc[NPROC]; ++p) {
        if (p != thiscpu->proc) {
            acquire(&p->lock);
            if (p->state == SLEEPING && p->chan == chan) {
                p->state = RUNNABLE;
            }
            release(&p->lock);
        }
    }
}

/*
 * Give up the CPU for one scheduling round.
 */
void
yield()
{
    struct proc* p = thiscpu->proc;
    acquire(&p->lock);
    p->state = RUNNABLE;
    cprintf("yield: proc %d gives up CPU %d.\n", p->pid, cpuid());
    sched();
    release(&p->lock);
}

/*
 * Create a new process copying p as the parent.
 * Sets up stack to return as if from system call.
 * Caller must set state of returned proc to RUNNABLE.
 */
int
fork()
{
    /* TODO: Your code here. */
}

/*
 * Wait for a child process to exit and return its pid.
 * Return -1 if this process has no children.
 */
int
wait()
{
    /* TODO: Your code here. */
}

/*
 * Print a process listing to console.  For debugging.
 * Runs when user types ^P on console.
 * No lock to avoid wedging a stuck machine further.
 */
void
procdump()
{
    panic("unimplemented");
}
