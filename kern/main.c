#include <stdint.h>

#include "arm.h"
#include "console.h"
#include "kalloc.h"
#include "string.h"
#include "timer.h"
#include "trap.h"
#include "vm.h"

void
main()
{
    /*
     * Before doing anything else, we need to ensure that all
     * static/global variables start out zero.
     */

    extern char edata[], end[], vectors[];

    memset(edata, 0, end - edata);
    console_init();
    alloc_init();
    cprintf("Allocator: Init success.\n");
    check_map_region();
    check_free_list();

    irq_init();

    lvbar(vectors);
    timer_init();

    while (1) {}
}
