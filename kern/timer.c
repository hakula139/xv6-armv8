#include "timer.h"

#include "arm.h"
#include "peripherals/irq.h"

#include "console.h"

static int dt = 19200000;

void
timer_init()
{
    asm volatile("msr cntp_ctl_el0, %[x]" : : [x] "r"(1));
    asm volatile("msr cntp_tval_el0, %[x]" : : [x] "r"(dt));
    put32(CORE_TIMER_CTRL(cpuid()), CORE_TIMER_ENABLE);
    cprintf("timer_init: success.\n");
}

void
timer_reset()
{
    asm volatile("msr cntp_tval_el0, %[x]" : : [x] "r"(dt));
}

/*
 * This is a per-cpu non-stable version of clock, frequency of
 * which is determined by cpu clock (may be tuned for power saving).
 */
void
timer()
{
    cprintf("timer: CPU %d timer.\n", cpuid());
}
