#include "trap.h"

#include "arm.h"
#include "mmu.h"
#include "peripherals/irq.h"
#include "syscall.h"
#include "sysregs.h"

#include "clock.h"
#include "console.h"
#include "proc.h"
#include "timer.h"
#include "uart.h"

void
irq_init()
{
    clock_init();
    put32(ENABLE_IRQS_1, AUX_INT);
    put32(GPU_INT_ROUTE, GPU_IRQ2CORE(0));
    cprintf("irq_init: success.\n");
}

void
trap(struct trapframe* tf)
{
    struct proc* p = thiscpu->proc;
    int src = get32(IRQ_SRC_CORE(cpuid()));
    int bad = 0;
    if (src & IRQ_CNTPNSIRQ) {
        timer(), timer_reset(), yield();
    } else if (src & IRQ_TIMER) {
        clock(), clock_reset();
    } else if (src & IRQ_GPU) {
        if (get32(IRQ_PENDING_1) & AUX_INT)
            uart_intr();
        else
            bad = 1;
    } else {
        switch (resr() >> EC_SHIFT) {
        case EC_SVC64:
            lesr(0); /* Clear esr. */
            /* Jump to syscall to handle the system call from user process */
            if (p->killed) exit(-1);
            p->tf = tf;
            syscall();
            if (p->killed) exit(-1);
            break;
        default: bad = 1;
        }
    }
    if (bad) panic("\ttrap: unexpected irq.\n");
}

void
irq_error(uint64_t type)
{
    panic("\tirq_error: irq of type %d unimplemented.\n", type);
}
