#ifndef INC_VM_H
#define INC_VM_H

#include "memlayout.h"

void vm_free(uint64_t*, int);
void check_map_region();

#endif  // INC_VM_H
