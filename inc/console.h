#ifndef INC_CONSOLE_H_
#define INC_CONSOLE_H_

#include <stdarg.h>

void console_init();
void console_intr(int (*getc)());
void cgetchar(int c);
void cprintf(const char* fmt, ...);
void panic(const char* fmt, ...);

#define assert(x)                                                              \
    ({                                                                         \
        if (!(x)) { panic("%s:%d: assertion failed.\n", __FILE__, __LINE__); } \
    })

/* Assertion with reason. */
#define asserts(x, ...)                                                        \
    ({                                                                         \
        if (!(x)) {                                                            \
            cprintf("%s:%d: assertion failed.\n", __FILE__, __LINE__);         \
            panic(__VA_ARGS__);                                                \
        }                                                                      \
    })

#endif  // INC_CONSOLE_H_
