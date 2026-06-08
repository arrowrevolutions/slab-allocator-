/* 
 * Copyright (c) 2026 Arrow Revolutions. All rights reserved.
 * Licensed under the Memory Allocator License (Non-Commercial, Anti-Aggressive Forking).
 * Project repository: https://github.com/arrowrevolutions/slab-allocator-/tree/main
 */
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
}slab_desc __attribute__ ((packed));

typedef struct master_desc{
  unsigned long free_amount;
  unsigned long head_off;
  struct master_desc* next;
  struct master_desc* prev;
}master_desc __attribute__ ((packed));


extern __attribute__ ((always_inline)) inline void* slab_alloc(addr_t size);

extern __attribute__ ((always_inline)) inline void slab_dealloc(void* ptr);
#endif
