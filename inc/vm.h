#ifndef INC_VM_H_
#define INC_VM_H_

#include <stdint.h>

#include "file.h"
#include "proc.h"

void vm_free(uint64_t*, int);
void uvm_clear(uint64_t*, char*);
uint64_t* pgdir_init();
void uvm_init(uint64_t*, char*, uint64_t);
int uvm_load(uint64_t*, char*, struct inode*, uint64_t, uint64_t);
uint64_t uvm_alloc(uint64_t*, uint64_t, uint64_t);
uint64_t uvm_dealloc(uint64_t*, uint64_t, uint64_t);
void uvm_switch(struct proc*);
int uvm_copy(uint64_t*, uint64_t*, uint64_t);
int copyout(uint64_t*, uint64_t, char*, uint64_t);

void check_map_region();

#endif  // INC_VM_H_
