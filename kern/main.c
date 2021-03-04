#include <stdint.h>

#include "arm.h"
#include "buf.h"
#include "console.h"
#include "file.h"
#include "kalloc.h"
#include "proc.h"
#include "sd.h"
#include "spinlock.h"
#include "string.h"
#include "timer.h"
#include "trap.h"
#include "vm.h"

volatile static int started = 0;

void
main()
{
    extern char edata[], end[], vectors[];

    if (cpuid() == 0) {
        memset(edata, 0, end - edata);
        console_init();
        cprintf("main: [CPU 0] init started.\n");
        alloc_init();
        proc_init();
        lvbar(vectors);
        irq_init();
        timer_init();
        fileinit();
        sd_init();
        user_init();
        started = 1;  // allow APs to run
    } else {
        while (!started) {}
        cprintf("main: [CPU %d] init started.\n", cpuid());
        lvbar(vectors);
        timer_init();
    }

    cprintf("main: [CPU %d] init success.\n", cpuid());

    scheduler();
}
