#ifndef INC_VM_H_
#define INC_VM_H_

#include <stdint.h>

#include "proc.h"

void vm_free(uint64_t*, int);
void check_map_region();
void uvm_init(uint64_t*, char*, uint64_t);
uint64_t uvm_alloc(uint64_t*, uint64_t, uint64_t);
uint64_t uvm_dealloc(uint64_t*, uint64_t, uint64_t);
void uvm_switch(struct proc*);
int uvm_copy(uint64_t*, uint64_t*, uint64_t);

uint64_t* pgdir_init();

#endif  // INC_VM_H_
