/* 
 * Copyright (c) 2026 Arrow Revolutions. All rights reserved.
 * Licensed under the Memory Allocator License (Non-Commercial, Anti-Aggressive Forking).
 * Project repository: https://github.com/arrowrevolutions/slab-allocator-/tree/main
 */


#include "slab.h"
#include "memory-mng.h"
#include "buddy.h"
#include "../info/types.h"
#include "../debug.h"

typedef struct slab_chain{
    struct slab_chain* next;
    struct slab_chain* prev;
}slab_chain;
//from 16 bytes to 2048


#define SLAB_MIN_ORDER 4 //16
#define SLAB_MAX_ORDER 11 //2048
#define DESC_SIZE_LOG2 5 //32
#define DESC_SIZE (1<<5)
#define DELTA_SLAB_ORDER (SLAB_MAX_ORDER-SLAB_MIN_ORDER)
#define BLOCK_LOG2 MIN_ORDER
#define BLOCK_SIZE (1<<BLOCK_LOG2)
slab_desc* slab_freelist[DELTA_SLAB_ORDER+1];
//all the free slab descriptors


master_desc* free_place;

//free place shows where free descriptor chunks are

//for the first one-hart system it'll be good 


__attribute__ ((always_inline)) inline static slab_desc* get_desc(addr_t slab_order){
  master_desc* mdesc = free_place;
  //there are no null pointer protection because in crate_slab_desc there is already a protection for it.

  slab_desc* desc=(slab_desc*)((addr_t)mdesc)+(mdesc->head_off<<DESC_SIZE_LOG2);
  if(mdesc->free_amount > 1){
    if(desc->next!=(slab_desc*)0){
      mdesc->head_off = (((addr_t)desc->next)&(BLOCK_SIZE-1))>>DESC_SIZE_LOG2;
    }else{
      mdesc->head_off += 1; //it means that the next descriptor is not mapped
    }
  }else{
    free_place=free_place->next;
  }
  mdesc->free_amount -= 1;
  slab_desc* freelist = slab_freelist[slab_order-SLAB_MIN_ORDER];
  desc->next = freelist;
  if(freelist != (slab_desc*)0){
    freelist->prev = desc;
  }
  slab_freelist[slab_order-SLAB_MIN_ORDER] = desc;

  return desc;
}

__attribute__ ((always_inline)) inline static void return_desc(slab_desc* desc){
  master_desc* mdesc=(master_desc*)(((addr_t)desc)&(~(BLOCK_SIZE-1)));

  unsigned long head_offset=(((addr_t)desc)&(BLOCK_SIZE-1))>>DESC_SIZE_LOG2;
  if(mdesc->free_amount!=0){
    slab_desc* prev_desc=(slab_desc*)((addr_t)mdesc)+(mdesc->head_off<<DESC_SIZE_LOG2);
    prev_desc->prev=desc;
    desc->next=prev_desc;
  }
  mdesc->head_off=head_offset;
  mdesc->free_amount+=1;
  if(mdesc->free_amount == ((1<<(BLOCK_LOG2-DESC_SIZE_LOG2))-1)){
    mdl((void*)mdesc);
  }
}

__attribute__ ((always_inline)) inline static slab_desc* create_slab_desc(addr_t slab_order){
  master_desc* mdesc;
  if(free_place == (master_desc*)0){
    void* ptr = mlc(BLOCK_SIZE);

    if(ptr == (void*)0){
      return (slab_desc*)0;
    }
      
    mdesc=(master_desc*)ptr;
    mdesc->head_off = 1;
    mdesc->free_amount = (1<<((BLOCK_LOG2-DESC_SIZE_LOG2)+1))-1;
    mdesc->next = free_place;
    if(free_place != (master_desc*)0){
      free_place->prev = mdesc;
    }
    free_place=mdesc;
  }

  slab_desc* desc = get_desc(slab_order);

  desc->size = (1<<slab_order);
  desc->free_amount = (1<<(BLOCK_LOG2-slab_order));
  desc->head_off = 0;
  desc->ptr = mlc_slb((BLOCK_SIZE),desc);

  return desc;
}


__attribute__ ((always_inline)) inline void* slab_alloc(addr_t size){
  addr_t x=find_order(size); //instead of find_order(desc->size) bc they're the same
  if(x<SLAB_MIN_ORDER){
    x=SLAB_MIN_ORDER;
  }
  slab_desc* desc=slab_freelist[x-SLAB_MIN_ORDER];
  if(desc==(slab_desc*)0){
    desc=create_slab_desc(x);
  }
  void* ptr=desc->ptr;
  addr_t base_ptr=(addr_t)ptr;

  base_ptr+=desc->head_off<<x;
  slab_chain* chain=(slab_chain*)base_ptr;



  if(chain->next!=(slab_chain*)0){
    chain=chain->next;
    chain->prev->next=(slab_chain*)0;
    chain->prev=(slab_chain*)0;
    desc->head_off=((addr_t)chain-(addr_t)ptr)>>x;
  }else{
    if(desc->free_amount>1){
      desc->head_off+=1;
    }else{
      slab_freelist[x-SLAB_MIN_ORDER]=slab_freelist[x-SLAB_MIN_ORDER]->next;
    }
  }


  desc->free_amount-=1;
  return (void*)base_ptr;
}

inline void slab_dealloc(void* ptr){
  slab_desc* desc=show_slab(ptr);
  
  addr_t base=(addr_t)ptr;
  
  addr_t order=find_order(desc->size);

  base=(base&(BLOCK_SIZE-1))>>(order); //head off
  slab_chain* next_chain=(slab_chain*)ptr;
  next_chain->next=(slab_chain*)0;
  next_chain->prev=(slab_chain*)0;
  if(desc->free_amount!=0){
    addr_t near=((addr_t)ptr&(~(BLOCK_SIZE-1)))+(desc->head_off<<order);

    slab_chain* chain=(slab_chain*)near;

    if(desc->free_amount!=0 && chain->next==(slab_chain*)0){
      desc->head_off+=1;
      slab_chain* unmapped_chain=(slab_chain*)((((addr_t)ptr)&(~(BLOCK_SIZE-1)))+(desc->head_off<<order));
      chain->next=unmapped_chain;
      unmapped_chain->prev=chain;
    }
    next_chain->next=chain;
    /*due to the usage of the lifo algorithm. here next and prev stuff is not revesed. but... a little bit of wacky*/
    chain->prev=next_chain;
  }

  desc->head_off=base;
  desc->free_amount+=1;
  addr_t off=order-SLAB_MIN_ORDER;

  if(desc->free_amount==(BLOCK_SIZE>>order)){
    if(slab_freelist[off]==desc){
      if(slab_freelist[off]->next!=(slab_desc*)0 && slab_freelist[off]->next!=slab_freelist[off]){
        slab_freelist[off]=slab_freelist[off]->next;
      }else{
        slab_freelist[off]=(slab_desc*)0;
      }
    }
    if(desc->prev!=(slab_desc*)0){
      desc->prev->next=desc->next;
    }
    if(desc->next!=(slab_desc*)0){
        desc->next->prev=desc->prev;
    }
    desc->prev=(slab_desc*)0;//head off
    desc->next=(slab_desc*)0;
    addr_t* pt=(addr_t*)desc;
    //for use-after-free protection
    //why i didn't done that with the bottom? because it became this:
    void* ptr=desc->ptr;
    addr_t buddy_order=get_order(desc->ptr);
    pt[0]=0;
    pt[1]=0;
    buddy_dealloc(ptr,buddy_order);
    delete_slab(ptr);
    return_desc(desc);
  
  }else{
    if(slab_freelist[off]!=(slab_desc*)0 && slab_freelist[off]!=desc){
      slab_freelist[off]->next=desc;
      desc->prev=slab_freelist[off];
    }    
    slab_freelist[off]=desc;
  }
}
