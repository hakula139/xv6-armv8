#ifndef INC_SYSCALLNO_H_
#define INC_SYSCALLNO_H_

#define MAXARG 32 /* max exec arguments */

#define SYS_exec 0
#define SYS_exit 1

#define NELEM(x) (sizeof(x) / sizeof((x)[0]))

#endif  // INC_SYSCALLNO_H_