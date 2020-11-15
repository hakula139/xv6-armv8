#include <stdint.h>

#include "arm.h"
#include "console.h"
#include "kalloc.h"
#include "spinlock.h"
#include "string.h"
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
        memset(edata, 0, end - edata);
        console_init();
        cprintf("CPU 0: Init started.\n");
        alloc_init();
        cprintf("Allocator: Init success.\n");
        check_map_region();
        check_free_list();
        irq_init();
        lvbar(vectors);
        timer_init();
        started_lock.locked = 0;  // allow APs to run
    } else {
        while (started_lock.locked) {}
        cprintf("CPU %d: Init started.\n", cpuid());
        irq_init();
        lvbar(vectors);
        timer_init();
    }
    cprintf("CPU %d: Init success.\n", cpuid());

    while (1) {}
}
