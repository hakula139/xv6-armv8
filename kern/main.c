#include <stdint.h>

#include "arm.h"
#include "console.h"
#include "kalloc.h"
#include "proc.h"
#include "spinlock.h"
#include "timer.h"
#include "trap.h"
#include "vm.h"

struct spinlock started_lock = {1};

void
main()
{
    /*
     * Before doing anything else, we need to ensure that all
     * static/global variables start out zero.
     */

    extern char edata[], end[], vectors[];

    if (cpuid() == 0) {
        cprintf("main: [CPU 0] init started.\n");
        memset(edata, 0, end - edata);
        console_init();
        alloc_init();
        cprintf("main: allocator init success.\n");
        check_map_region();
        check_free_list();
        started_lock.locked = 0;  // allow APs to run
    } else {
        while (started_lock.locked) {}
        cprintf("main: [CPU %d] init started.\n", cpuid());
    }

    irq_init();
    proc_init();
    user_init();
    lvbar(vectors);
    timer_init();
    cprintf("main: [CPU %d] init success.\n", cpuid());

    scheduler();

    while (1) {}
}
