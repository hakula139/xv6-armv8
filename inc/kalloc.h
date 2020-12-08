#ifndef INC_KALLOC_H
#define INC_KALLOC_H

void alloc_init();
char* kalloc();
void kfree(char*);
void free_range(void*, void*);
void check_free_list();

#endif  // INC_KALLOC_H
