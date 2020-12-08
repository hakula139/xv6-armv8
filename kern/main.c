#include <stdint.h>

#include "console.h"
#include "string.h"

void
main()
{
    /*
     * Before doing anything else, we need to ensure that all
     * static/global variables start out zero.
     */

    extern char edata[], end[];
    memset(edata, 0, end - edata);

    console_init();
    cprintf("hello, world\n");

    while (1) {}
}
