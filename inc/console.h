#ifndef INC_CONSOLE_H_
#define INC_CONSOLE_H_

#include <stdarg.h>
#include <stdint.h>

void console_init();
void cgetchar(int c);
void cprintf(const char* fmt, ...);
void panic(const char* fmt, ...);

#define assert(x)                                                              \
    ({                                                                         \
        if (!(x)) {                                                            \
            cprintf("%s:%d: assertion failed.\n", __FILE__, __LINE__);         \
            while (1) {}                                                       \
        }                                                                      \
    })

#endif  // INC_CONSOLE_H_
