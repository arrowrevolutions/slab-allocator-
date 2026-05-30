#ifndef SLAB_H
#define SLAB_H

#include "../info/types.h"

typedef struct slab_desc{
  unsigned short size;
  unsigned short free_amount;
  unsigned short head_off;
  unsigned short padding;
  void* ptr;
  struct slab_desc* next;
  struct slab_desc* prev;
}slab_desc __attribute__((packed));

typedef struct master_desc{
  unsigned long free_amount;
  unsigned long head_off;
  unsigned long flag; //1 for valid
  unsigned long padding2;
}master_desc __attribute__((packed));


extern __attribute__ ((noinline)) void* slab_alloc(addr_t size);

extern __attribute__ ((noinline)) void slab_dealloc(void* ptr);
#endif
