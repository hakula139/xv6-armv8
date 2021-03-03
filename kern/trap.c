#include "trap.h"

#include <syscall.h>

#include "arm.h"
#include "clock.h"
#include "console.h"
#include "mmu.h"
#include "peripherals/irq.h"
#include "proc.h"
#include "sd.h"
#include "syscall1.h"
#include "sysregs.h"
#include "timer.h"
#include "uart.h"

void
irq_init()
{
    clock_init();
    put32(ENABLE_IRQS_1, AUX_INT);
    put32(ENABLE_IRQS_2, VC_ARASANSDIO_INT);
    put32(GPU_INT_ROUTE, GPU_IRQ2CORE(0));
    cprintf("irq_init: success.\n");
}

void
interrupt(struct trapframe* tf)
{
    int src = get32(IRQ_SRC_CORE(cpuid()));
    if (src & IRQ_CNTPNSIRQ) {
        timer_reset();
        timer();
        yield();
    } else if (src & IRQ_TIMER) {
        clock_reset();
        clock();
    } else if (src & IRQ_GPU) {
        int p1 = get32(IRQ_PENDING_1);
        int p2 = get32(IRQ_PENDING_2);
        if (p1 & AUX_INT) {
            uart_intr();
        } else if (p2 & VC_ARASANSDIO_INT) {
            sd_intr();
        } else {
            cprintf(
                "interrupt: unexpected gpu intr p1 %x, p2 %x, sd %d, omitted.\n",
                p1, p2, p2 & VC_ARASANSDIO_INT);
        }
    } else {
        cprintf("interrupt: unexpected interrupt at CPU %d\n", cpuid());
    }
}

void
trap(struct trapframe* tf)
{
    int ec = resr() >> EC_SHIFT, iss = resr() & ISS_MASK;
    lesr(0);  // Clear esr.
    switch (ec) {
    case EC_UNKNOWN: interrupt(tf); break;
    case EC_SVC64:
        if (!iss) {
            /* Jump to syscall to handle the system call from user process */
            tf->x0 = syscall1(tf);
        } else {
            cprintf("trap: unexpected svc iss 0x%x\n", iss);
        }
        break;
    default: panic("\ttrap: unexpected irq.\n");
    }
}

void
irq_error(uint64_t type)
{
    panic("\tirq_error: irq of type %d unimplemented.\n", type);
}
