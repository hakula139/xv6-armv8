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
    cprintf("irq_init: success at CPU %d.\n", cpuid());
}

void
trap(struct trapframe* tf)
{
    struct proc* proc = thiscpu->proc;
    int src = get32(IRQ_SRC_CORE(cpuid()));
    if (src & IRQ_CNTPNSIRQ) {
        timer(), timer_reset();
    } else if (src & IRQ_TIMER) {
        clock(), clock_reset();
    } else if (src & IRQ_GPU) {
        if (get32(IRQ_PENDING_1) & AUX_INT)
            uart_intr();
        else
            goto bad;
    } else {
        switch (resr() >> EC_SHIFT) {
        case EC_SVC64:
            lesr(0); /* Clear esr. */
            /* Jump to syscall to handle the system call from user process */
            /* TODO: Your code here. */
            break;
        default:
        bad:
            panic("trap: unexpected irq.\n");
        }
    }
}

void
irq_error(uint64_t type)
{
    panic("irq_error: irq of type %d unimplemented.\n", type);
}
