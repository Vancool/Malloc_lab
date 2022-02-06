#include "my_malloc.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
/**
 * @brief my simple version malloc libary
 * 
 * Creator: Yifan(Eva) Lin
 * NetId: yl734
 *  
 * tracking free block method: one level explicit free lists, insert block to list with LIFO
 * 
 * data structure of an available block:
 *                  free Metadata address
 *                          ^
 *                          | 
 *     | size | is_alloc | prev | next | **other data** | footer(tail) |  
 *                                  |        |
 *                                  |        v
 *                                  v       payload
 *                        free  Metadata address 
 * 
 * reference: based on csapp & cmu15213
 * TODO:
 *    1. replace Metadata(size, is_alloc) == > Metadata(header)  header= PACK(size, is_alloc)
 *    2. remove the footer in allocated block ==> TAG in next block's header 
 *    3. one list ==> multi-level size explict list
 *    4. reallocate function
 */


/*Basic constants and macros
  Used only in my_malloc.c
*/

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
static void * reuse_block(size_t size, Metadata * p);
static void * allocate_block(size_t size);
static void* my_sbrk(size_t size);
static void remove_block(Metadata * p);
static void set_tail(Metadata *p);
static void add_block_to_head(Metadata *p);
static void coalesc_tail(Metadata *p);

Metadata * first_free_block = NULL; // free block list header
Metadata * last_free_block = NULL; // free block list tail
size_t data_size = 0;  // record allocated data
size_t free_size = 0; // record available data
void *begin = NULL; //use to check invalid address
size_t op = 0; // debug

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
static void * reuse_block(size_t size, Metadata * p) {
  if (p->size - size >= sizeof(Metadata)+DSIZE+TAIL_SIZE) {
    Metadata * remain = (Metadata *)((char *)p + sizeof(Metadata) + size+TAIL_SIZE);
    remain->size = p->size - size - sizeof(Metadata)-TAIL_SIZE;
    remain->is_alloc = UNALLOC;
    remain->next = p->next;
    if(p == last_free_block){
      last_free_block = remain;
    }else{
      if(p->next) p->next->prev = remain;
    }
    p->next = remain;
    remain->prev = p;
    p->size = size; 
    set_tail(remain);
  }
  free_size -= p->size + sizeof(Metadata)+TAIL_SIZE;
  remove_block(p);
  p->is_alloc = ALLOC;
  set_tail(p);
  return (char *)p + sizeof(Metadata);
}

// wrapper sbrk function to record allocated data
static void* my_sbrk(size_t size){
  void *res = sbrk(size);
  if(res != (void*)-1){
    data_size += size;
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

// add availble block as head of free block list
static void add_block_to_head(Metadata *p){
  if(!first_free_block){
    first_free_block = p;
    last_free_block = p;
    p->next = NULL;
    p->prev = NULL;
    return;
  }
  p->next = first_free_block;
  first_free_block->prev = p;
  p->prev = NULL;
  first_free_block = p;
}

// remove the block
static void remove_block(Metadata * p) {
  if(first_free_block == p){
    first_free_block = p->next; // if p is last node, p->next = NULL
    if(p == last_free_block) last_free_block = NULL;
    if(p->next != NULL) p->next->prev = NULL;
  }
  else{
    if(p->next!= NULL)p->next->prev = p->prev;
    p->prev->next = p->next;
    if(p == last_free_block) last_free_block = p->prev; 
  }
  p->next = NULL;
  p->prev = NULL;
}

// use footer to coalesc block with previous one and next one
static void coalesc_tail(Metadata *p){
  if(!p) return;
  if((void*)p + sizeof(Metadata) + p->size+TAIL_SIZE < sbrk(0)){
    Metadata *pnext = (Metadata*)((void*)p + sizeof(Metadata) + p->size+TAIL_SIZE);
    if(pnext->is_alloc == UNALLOC){
      p->size += sizeof(Metadata) + pnext->size + TAIL_SIZE;
      remove_block(pnext);
      set_tail(p);
    }
  }
  void *prev_tail = (void *)p - TAIL_SIZE;
  if(prev_tail > begin && prev_tail - GET_SIZE(prev_tail) - sizeof(Metadata) > begin){
    if (GET_ALLOC(prev_tail) == UNALLOC){
      Metadata *pprev = (Metadata*)(prev_tail - GET_SIZE(prev_tail) - sizeof(Metadata));
      pprev->size += sizeof(Metadata) + TAIL_SIZE + p->size;
      set_tail(pprev);
      remove_block(p);
    }
  }
}


/** user function **/

void * ff_malloc(size_t size) {
  if(begin == NULL) begin = sbrk(0);
  Metadata * p = first_free_block;
  while (p && p->size < size) p = p->next;
  if(p) return reuse_block(size, p);
  return allocate_block(size);
}


void ff_free(void * ptr) {
  Metadata * p = (Metadata *)((char *)ptr - sizeof(Metadata));
  p->is_alloc = UNALLOC;
  free_size += p->size + sizeof(Metadata)+TAIL_SIZE;
  set_tail(p);
  add_block_to_head(p);
  coalesc_tail(p);
}

void * bf_malloc(size_t size) {
  if(begin == NULL) begin = sbrk(0);
  Metadata * p = first_free_block;
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
    return reuse_block(size, best);
  }
  else {
    return allocate_block(size);
  }
}

void bf_free(void * ptr) {
  return ff_free(ptr);
}

unsigned long get_data_segment_size() {
  return data_size;
}

unsigned long get_data_segment_free_space_size() {
  return free_size;
}
