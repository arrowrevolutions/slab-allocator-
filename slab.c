/* 
 * Copyright (c) 2026 Arrow Revolutions. All rights reserved.
 * Licensed under the Memory Allocator License (Non-Commercial, Anti-Aggressive Forking).
 * Project repository:https://github.com/arrowrevolutions/slab-allocator-/tree/main
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


slab_chain* free_place;
//for the first one-hart system it'll be good 


__attribute__ ((always_inline)) static slab_desc* create_slab_desc(addr_t slab_order){
  master_desc* mdesc;
  //unsigned char flg=0;
  if(free_place==(slab_chain*)0){
      void* ptr=mlc(BLOCK_SIZE);
      uart_puthex((addr_t)ptr);

      if(ptr==(void*)0){
          return (slab_desc*)0;
      }
      
      free_place=(slab_chain*)(((addr_t)ptr)+DESC_SIZE);
      mdesc=(master_desc*)ptr;
      mdesc->head_off=1;
      mdesc->free_amount=(1<<((BLOCK_LOG2-DESC_SIZE_LOG2)+1))-1;
      mdesc->flag=1;
  }
  

  slab_desc* desc=(slab_desc*)free_place;
  mdesc=(master_desc*)(((addr_t)free_place)&(~(BLOCK_SIZE-1)));
  if(free_place->next==(slab_chain*)0 && mdesc->free_amount>1){ //it means thet they are some not mapped descs
    free_place=(slab_chain*)(addr_t)free_place+DESC_SIZE;
  }else{
    free_place=free_place->next;
  }

  if(free_place!=(slab_chain*)0){
    mdesc->head_off=((addr_t)free_place&(BLOCK_SIZE-1))>>DESC_SIZE_LOG2;// dividing desc size to find a headoff
  }
  mdesc->free_amount-=1;
  if(free_place!=(slab_chain*)0){
    free_place->prev=(slab_chain*)0; //wipes off the previous one bc it's already is using.
  }
  desc->size=(1<<slab_order);
  desc->free_amount=(1<<(BLOCK_LOG2-slab_order));
  desc->head_off=0;
  desc->ptr=mlc_slb((BLOCK_SIZE),desc);


  addr_t pos=slab_order-SLAB_MIN_ORDER;

  desc->prev=slab_freelist[pos];
  if(slab_freelist[pos]!=(slab_desc*)0){
    slab_freelist[pos]->next=desc;
  }
  slab_freelist[pos]=desc;
  return desc;
}


void* slab_alloc(addr_t size){
  addr_t x=find_order(size);
  if(x<SLAB_MIN_ORDER){
    x=SLAB_MIN_ORDER;
  }
  slab_desc* desc=slab_freelist[x-SLAB_MIN_ORDER];
  if(desc==(slab_desc*)0){
    desc=create_slab_desc(x);
  }
  addr_t base_ptr=(addr_t)desc->ptr;


  base_ptr+=desc->size*desc->head_off;
  slab_chain* chain=(slab_chain*)base_ptr;

  if(chain->next!=(slab_chain*)0){
    chain=chain->next;
    desc->head_off=((addr_t)chain-(addr_t)desc->ptr)/desc->size;
  }else{
    if(desc->free_amount!=0){
      desc->head_off+=1;
    }
  }


  desc->free_amount-=1;
  return (void*)base_ptr;


}

void slab_dealloc(void* ptr){
  slab_desc* desc=show_slab(ptr);
  
  addr_t base=(addr_t)ptr;
  
  base=(base&(BLOCK_SIZE-1))/desc->size; //head off

  addr_t near=((addr_t)ptr&(~(BLOCK_SIZE-1)))+(desc->size*desc->head_off);

  slab_chain* chain=(slab_chain*)near;
  slab_chain* next_chain=(slab_chain*)ptr;

  if(next_chain->next!=(slab_chain*)0){
    chain->next=next_chain->next;
  }else if(desc->free_amount>1){
    chain->next=(slab_chain*)((((addr_t)ptr)&(~(BLOCK_SIZE-1)))+(desc->size * desc->head_off));
        desc->head_off+=1;

  }


  next_chain->next=chain;
  /*due to the usage of the lifo algorithm. here next and prev stuff is not revesed. but... a little bit of wacky*/

  chain->prev=next_chain;
  desc->head_off=base;
  desc->free_amount+=1;
  addr_t off=find_order(desc->size)-SLAB_MIN_ORDER;

  if(desc->free_amount==(BLOCK_SIZE/desc->size)){
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
    //why i didn't done that with the bottom? bc it became this:
    void* ptr=desc->ptr;
    addr_t order=get_order(desc->ptr);
    pt[0]=0;
    pt[1]=0;
    buddy_dealloc(ptr,order);
    delete_slab(ptr);
    master_desc* mdesc=(master_desc*)((addr_t)desc&(~(BLOCK_SIZE-1)));
    mdesc->free_amount+=1;
    if(mdesc->free_amount==127){ //the maximum amount of desc are 127 bc master desc takes one of them
      mdesc->free_amount=0;
      mdesc->head_off=0;
      mdesc->flag=0;
      //use-after-free protection
      buddy_dealloc((void*)mdesc,MIN_ORDER);
    }else{
      slab_chain* desc_chain=(slab_chain*)desc;
      desc_chain->next=free_place;
      desc_chain->prev=(slab_chain*)0;
      //natural use-after-free!
      if(free_place!=(slab_chain*)0){
        free_place->prev=desc_chain;
      }
      free_place=desc_chain;
    }
  }else{
    if(slab_freelist[off]!=(slab_desc*)0 && slab_freelist[off]!=desc){
      slab_freelist[off]->next=desc;
    }    
    slab_freelist[off]=desc;
  }
}

