#include "my_malloc.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#define With_sbk_lock 1
#define No_sbk_lock 0
#define WSIZE 4  //linux:1 word = 4 bytes, extendable alteration when system change
#define DSIZE 8  // the threshold of inner fragment of each block < DSIZE 
#define GET(p)  (*(size_t *)(p))
#define PUT(p, val)  (*(size_t *)(p) = (val))
#define PACK(size, is_alloc)  ((size)|(is_alloc))
#define GET_SIZE(p)  (GET(p) & ~0x1) 
#define GET_ALLOC(p) (GET(p) & 0x1)
#define UNALLOC 1
#define ALLOC 0
#define TAIL_SIZE (sizeof(size_t))

struct metadata {
  size_t size;
  size_t is_alloc;
  struct metadata * next;
  struct metadata * prev;
};
typedef struct metadata Metadata;

// static function signature
// static void * reuse_block(size_t size, Metadata * p);
static void * allocate_block(size_t size);
static void *allocate_block_with_lock(size_t size);
static void* my_sbrk(size_t size);
static void* my_sbrk_with_lock(size_t size);
// static void remove_block(Metadata * p);
static void set_tail(Metadata *p);
// static void add_block_to_head(Metadata *p);
// static void coalesc_tail(Metadata *p);

static void coalesc_tail(Metadata *p, Metadata ** first_free_block, Metadata ** last_free_block);
static void * reuse_block(size_t size, Metadata * p, Metadata ** first_free_block, Metadata **last_free_block);
static void add_block_to_head(Metadata *p,  Metadata ** first_free_block, Metadata ** last_free_block);
static void remove_block(Metadata * p,  Metadata ** first_free_block, Metadata ** last_free_block);
static void coalesc_tail(Metadata *p, Metadata ** first_free_block, Metadata ** last_free_block);

Metadata * first_free_lock = NULL;
Metadata * last_free_lock = NULL;

__thread Metadata * first_free_block_th = NULL; // free block list header
__thread Metadata * last_free_block_th = NULL; // free block list tail


size_t data_size = 0;  // record allocated data
size_t free_size = 0; // record available data
void *begin = NULL; //use to check invalid address
void *begin_th = NULL;

size_t op = 0; // debug

// pthread lock
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER; // mutex for thread
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER; // mutex for sbrk


// function to do alignment, unnessary in this experiment
static size_t align(size_t size){
  return (size+DSIZE-1) / DSIZE * DSIZE;
}

// set the footer for block
static void set_tail(Metadata *p){
  size_t size = p->size;
  size_t is_alloc = p->is_alloc;
  void *tail_ptr = (void*)p + size + sizeof(Metadata);
  PUT(tail_ptr, PACK(size, is_alloc));
}

// reuse previous block
static void * reuse_block(size_t size, Metadata * p, Metadata ** first_free_block, Metadata **last_free_block) {
  if (p->size - size >= sizeof(Metadata)+DSIZE+TAIL_SIZE) {
    Metadata * remain = (Metadata *)((char *)p + sizeof(Metadata) + size+TAIL_SIZE);
    remain->size = p->size - size - sizeof(Metadata)-TAIL_SIZE;
    remain->is_alloc = UNALLOC;
    // remain->next = p->next;
//     if(p == *last_free_block){
//       *last_free_block = remain;
//     }else{
//       if(p->next) p->next->prev = remain;
//     }
    // p->next = remain;
    // remain->prev = p;
    add_block_to_head(remain, first_free_block, last_free_block);
    p->size = size; 
    set_tail(remain);
  }
  free_size -= p->size + sizeof(Metadata)+TAIL_SIZE;
  remove_block(p, first_free_block, last_free_block);
  p->is_alloc = ALLOC;
  set_tail(p);
  return (char *)p + sizeof(Metadata);
}

// wrapper sbrk function to record allocated data
static void* my_sbrk(size_t size){
  if(begin == NULL){
    begin = sbrk(0);
  }
  void *res = sbrk(size);
  if(res != (void*)-1){
    // data_size += size
    return res;
  }
  return NULL;
}

// expend heap size to create new block
static void * allocate_block(size_t size) {
  Metadata * new_block = my_sbrk(size + sizeof(Metadata)+TAIL_SIZE);
  if(new_block == NULL){
    return NULL;
  }
  new_block->size = size;
  new_block->is_alloc = ALLOC;
  new_block->prev = NULL;
  new_block->next = NULL;
  set_tail(new_block);
  return (void *)new_block + sizeof(Metadata);
}

static void* my_sbrk_with_lock(size_t size){
  pthread_mutex_lock(&mutex);
  if(begin_th == NULL){
    begin_th = sbrk(0);
  }
  void *res = sbrk(size);
  // end_th = res;
  pthread_mutex_unlock(&mutex);
  if(res != (void*)-1){
    data_size += size;
    return res;
  }
  return NULL;
}

static void *allocate_block_with_lock(size_t size){
  Metadata * new_block = my_sbrk_with_lock(size + sizeof(Metadata)+TAIL_SIZE);
  if(new_block == NULL){
    return NULL;
  }
  new_block->size = size;
  new_block->is_alloc = ALLOC;
  new_block->prev = NULL;
  new_block->next = NULL;
  set_tail(new_block);
  return (void *)new_block + sizeof(Metadata);

}

// add availble block as head of free block list
static void add_block_to_head(Metadata *p,  Metadata **first_free_block, Metadata ** last_free_block){
  if(!*first_free_block){
    *first_free_block = p;
    *last_free_block = p;
    p->next = NULL;
    p->prev = NULL;
    return;
  }
  p->next = *first_free_block;
  (*first_free_block) -> prev = p;
  p->prev = NULL;
  *first_free_block = p;
}

// remove the block
static void remove_block(Metadata * p,  Metadata ** first_free_block, Metadata ** last_free_block) {
  if(*first_free_block == p){
    *first_free_block = p->next; // if p is last node, p->next = NULL
    if(p == *last_free_block) *last_free_block = NULL;
    if(p->next != NULL) p->next->prev = NULL;
  }
  else{
    if(p->next!= NULL)p->next->prev = p->prev;
    p->prev->next = p->next;
    if(p == *last_free_block) *last_free_block = p->prev; 
  }
  p->next = NULL;
  p->prev = NULL;
}

// use footer to coalesc block with previous one and next one
static void coalesc_tail(Metadata *p, Metadata ** first_free_block, Metadata ** last_free_block){
  if(!p) return;
  if((void*)p + sizeof(Metadata) + p->size+TAIL_SIZE < sbrk(0)){
    Metadata *pnext = (Metadata*)((void*)p + sizeof(Metadata) + p->size+TAIL_SIZE);
    if(pnext->is_alloc == UNALLOC){
      p->size += sizeof(Metadata) + pnext->size + TAIL_SIZE;
      remove_block(pnext,first_free_block, last_free_block);
      set_tail(p);
    }
  }
  // if(((void *)p != (void*)last_free_block)&& p->next&& (void*)p->next == (void*)p+sizeof(Metadata)+p->size+TAIL_SIZE){
  //   if(p->next->is)
  //   p->size += sizeof(Metadata) + p->next->size + TAIL_SIZE;
  //   remove_block(p->next, first_free_block, last_free_block);
  //   set_tail(p);
  // }
  void *prev_tail = (void *)p - TAIL_SIZE;
  if(prev_tail > begin && prev_tail - GET_SIZE(prev_tail) - sizeof(Metadata) > begin){
    if (GET_ALLOC(prev_tail) == UNALLOC){
      Metadata *pprev = (Metadata*)(prev_tail - GET_SIZE(prev_tail) - sizeof(Metadata));
      pprev->size += sizeof(Metadata) + TAIL_SIZE + p->size;
      set_tail(pprev);
      remove_block(p, first_free_block, last_free_block);
    }
  }
}

static void coalesc_tail_with_lock(Metadata *p, Metadata ** first_free_block, Metadata ** last_free_block){
  if(!p) return;
  if((void*)p + sizeof(Metadata) + p->size+TAIL_SIZE < sbrk(0)){
    Metadata *pnext = (Metadata*)((void*)p + sizeof(Metadata) + p->size+TAIL_SIZE);
    if(pnext->is_alloc == UNALLOC){
      remove_block(pnext, first_free_block, last_free_block);
      p->size += sizeof(Metadata) + pnext->size + TAIL_SIZE;
      set_tail(p);
    }
  }
  void *prev_tail = (void *)p - TAIL_SIZE;
  if(prev_tail > begin && prev_tail - GET_SIZE(prev_tail) - sizeof(Metadata) > begin){
    if (GET_ALLOC(prev_tail) == UNALLOC){
      Metadata *pprev = (Metadata*)(prev_tail - GET_SIZE(prev_tail) - sizeof(Metadata));
      pprev->size += sizeof(Metadata) + TAIL_SIZE + p->size;
      set_tail(pprev);
      remove_block(p, first_free_block, last_free_block);
    }
  }
}
/** user function **/

void ff_free(void * ptr, int with_sbk_lock, Metadata ** first_free_block, Metadata ** last_free_block) {
  Metadata * p = (Metadata *)((char *)ptr - sizeof(Metadata));
  p->is_alloc = UNALLOC;
  free_size += p->size + sizeof(Metadata)+TAIL_SIZE;
  set_tail(p);
  add_block_to_head(p, first_free_block, last_free_block);
  // if(with_sbk_lock){
  //   coalesc_tail_with_lock(p, first_free_block, last_free_block);
  // }else{
  coalesc_tail(p, first_free_block, last_free_block);
  // }
}

void * bf_malloc(size_t size, int with_sbk_lock, Metadata **first_free_block, Metadata **last_free_block) {
  Metadata * p = *first_free_block;
  Metadata * best = NULL;
  while (p != NULL) {
    if (p->size >= size) {
      if ((!best) || (p->size < best->size)) {
        best = p;
        // the threshold of inner fragment < DSIZE 
        if(best->size-size < DSIZE){ 
            break; // use immediately
        }
      }
    }
    p = p->next;
  }
  if (best) {
    return reuse_block(size, best, first_free_block, last_free_block);
  }
  else {
    // if(with_sbk_lock){
    //   return allocate_block_with_lock(size);
    // }
    return allocate_block(size);
  }
}

void bf_free(void * ptr, int with_sbk_lock, Metadata **first_free_block, Metadata **last_free_block) {
  return ff_free(ptr, with_sbk_lock, first_free_block, last_free_block);
}

unsigned long get_data_segment_size() {
  return data_size;
}

unsigned long get_data_segment_free_space_size() {
  return free_size;
}

void * ts_malloc_lock(size_t size) {
  pthread_mutex_lock(&lock);
  void * p = bf_malloc(size, No_sbk_lock, &first_free_lock, &last_free_lock);
  pthread_mutex_unlock(&lock);
  return p;
}
void ts_free_lock(void * ptr) {
  pthread_mutex_lock(&lock);
  bf_free(ptr, No_sbk_lock, &first_free_lock, &last_free_lock);
  pthread_mutex_unlock(&lock);
}

/* 
*
*  Another version of bf_malloc(Maintain an order linklist in , whose insert time complexity O(n))
*
*/

static void * reuse_block_th(size_t size, Metadata * p, Metadata **first_free_block, Metadata ** last_free_block);
static void * allocate_block_th(size_t size);
// static void* my_sbrk(size_t size);
static void add_block_th(Metadata * p, Metadata **first_free_block, Metadata ** last_free_block);
static void remove_block_th(Metadata * p, Metadata **first_free_block, Metadata ** last_free_block);
static Metadata* coalesc_th(Metadata *p, Metadata **first_free_block, Metadata ** last_free_block);

static void remove_block_th(Metadata * p, Metadata **first_free_block, Metadata ** last_free_block) {
  if(*first_free_block == p){
    *first_free_block = p->next; // if p is last node, p->next = NULL
    if(p == *last_free_block) *last_free_block = NULL;
    if(p->next != NULL) p->next->prev = NULL;
  }
  else{
    if(p->next!= NULL)p->next->prev = p->prev;
    p->prev->next = p->next;
    if(p == *last_free_block) *last_free_block = p->prev; 
  }
  p->next = NULL;
  p->prev = NULL;
}


static void * reuse_block_th(size_t size, Metadata * p, Metadata **first_free_block, Metadata ** last_free_block) {
  if (p->size > size + sizeof(Metadata)) {
    Metadata * remain = (Metadata *)((char *)p + sizeof(Metadata) + size);
    remain->size = p->size - size - sizeof(Metadata);
    remain->is_alloc = UNALLOC;
    remain->next = p->next;
    if(p == *last_free_block){
      *last_free_block = remain;
    }else{
      if(p->next) p->next->prev = remain;
    }
    p->next = remain;
    p->size = size;
    remain->prev = p; // new added
  }
  free_size -= p->size + sizeof(Metadata);
  remove_block_th(p, first_free_block, last_free_block);
  p->is_alloc = ALLOC;
  return (char *)p + sizeof(Metadata);
}


static void * allocate_block_th(size_t size) {
  Metadata * new_block = my_sbrk_with_lock(size + sizeof(Metadata));
  if(new_block == NULL){
    return NULL;
  }
  new_block->size = size;
  new_block->is_alloc = ALLOC;
  // new_block->prev = last_free_block; //shortcut
  new_block->prev = NULL;
  new_block->next = NULL;
  return (void *)new_block + sizeof(Metadata);
}

// different with verision 1 add block, it need to traverse to find its right positon
static void add_block_th(Metadata * p, Metadata **first_free_block, Metadata ** last_free_block) {
  if(*first_free_block == NULL){
    *first_free_block = p;
    *last_free_block = p;
    p->prev = NULL;
    p->next = NULL;
    return;
  }
  if(p > *last_free_block){
    (*last_free_block)->next = p;
    p->next = NULL;
    p->prev = *last_free_block;
    *last_free_block = p;
    return;
  }
  if(p < *first_free_block){
    p->prev = NULL;
    p->next = *first_free_block;
    (*first_free_block) -> prev = p;
    *first_free_block = p; 
  }else{
    Metadata *prev = *first_free_block;
    while(prev->next && prev->next < p){
      prev = prev->next;
    }
    if(prev->next){
      prev->next->prev = p;
      // p->next = prev->next;
    }
    p->next = prev->next;
    p->prev = prev;
    prev->next = p;
  }
}


static Metadata* coalesc_th(Metadata *p, Metadata **first_free_block, Metadata ** last_free_block){
    if(!p) return NULL;
    if(p->next && (void*)p + p->size+sizeof(Metadata) == (void*)(p->next)){
      p->size += sizeof(Metadata) + p->next->size;
      remove_block_th(p->next, first_free_block, last_free_block);
    }
    return p;
}

void bf_free_th(void * ptr, Metadata **first_free_block, Metadata ** last_free_block) {
  Metadata * p = (Metadata *)((char *)ptr - sizeof(Metadata));
  p->is_alloc = UNALLOC;
  free_size += p->size + sizeof(Metadata);
  add_block_th(p, first_free_block, last_free_block);
  if(p != *last_free_block)coalesc_th(p, first_free_block, last_free_block);
  if(p != *first_free_block)coalesc_th(p->prev, first_free_block, last_free_block);
}


void * bf_malloc_th(size_t size, Metadata **first_free_block, Metadata ** last_free_block) {
  Metadata * p = *first_free_block;
  Metadata * best = NULL;
  while (p != NULL) {
    if (p->size >= size) {
      if ((!best) || (p->size < best->size)) {
        best = p;
        if(best->size-size < DSIZE){
            break; // use immediately
        }
      }
    }
    p = p->next;
  }
  if (best) {
    return reuse_block_th(size, best,first_free_block, last_free_block);
  }
  else {
    return allocate_block_th(size);
  }
}


void *ts_malloc_nolock(size_t size){
  return bf_malloc_th(size, &first_free_block_th, &last_free_block_th);
  // return bf_malloc(size, With_sbk_lock, &first_free_block_th, &last_free_block_th);
}
void ts_free_nolock(void *ptr){
  bf_free_th(ptr, &first_free_block_th, &last_free_block_th);
  // bf_free(ptr, With_sbk_lock, &first_free_block_th, &last_free_block_th);
}