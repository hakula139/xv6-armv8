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

static struct spinlock start_lock = {0};
volatile static int started = 0;

void
main()
{
    extern char edata[], end[], vectors[];

    acquire(&start_lock);
    if (!started) {
        memset(edata, 0, end - edata);
        console_init();
        cprintf("main: [CPU %d] init started.\n", cpuid());
        alloc_init();
        proc_init();
        lvbar(vectors);
        irq_init();
        timer_init();
        file_init();
        binit();
        sd_init();
        user_init();
        started = 1;
        release(&start_lock);  // allow APs to run
    } else {
        release(&start_lock);
        cprintf("main: [CPU %d] init started.\n", cpuid());
        lvbar(vectors);
        timer_init();
    }
    cprintf("main: [CPU %d] init success.\n", cpuid());

    scheduler();
}
