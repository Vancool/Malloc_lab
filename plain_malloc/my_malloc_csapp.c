#include "my_malloc.h"
/**
 * @brief my malloc lib
 * 
 * Creator: Yifan(Eva) Lin
 * NetId: yl734
 *  
 * tracking free block method: multi-level explicit free lists, insert block to list with LIFO
 * 
 * data structure of an available block:
 *               other block playload address
 *                  ^
 *                  | 
 *     | header | prev | next | **other data** | footer |  
 *              |          |
 *              bp         v
 *                       other block payload address
 * 
 * reference: based on csapp & cmu15213
 * TODO:
 *    1. remove the footer in allocated block with TAG in next block's header
 *    2. realloc function
 */


/*Basic constants and macros
  Used only in my_malloc.c
*/

#define WSIZE sizeof(char*)  //linux:1 word = 4 bytes, extendable alteration when system change
#define DSIZE WSIZE * 2
#define OVERHEAD WSIZE * 2 // header + footer + prev + next
#define MINSIZE DSIZE

#define GET(p)  (*(size_t *)(p))
#define PUT(p, val)  (*(size_t *)(p) = (val))
#define PACK(size, is_alloc)  ((size)|(is_alloc))
#define GET_SIZE(p)  (GET(p) & ~0x7) 
#define GET_ALLOC(p) (GET(p) & 0x1)


// get header and footer
#define HDRP(bp) ((char*)(bp) - WSIZE) //get header address from payload address
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - 2*WSIZE) // get footer address from payload address
#define GET_BP_SIZE(bp) (GET_SIZE(HDRP(bp)))
#define GET_BP_ALLOC(bp) (GET_ALLOC(HDRP(bp)))



// get physical prev and next address of bp
#define PRED(bp)  ((char*)(bp)- GET_SIZE((char*)(bp) - DSIZE))
#define NEXT(bp)  ((char*)(bp) + GET_BP_SIZE(bp))

// get free list prev and next address of bp
#define LIST_PREV_PTR(bp) ((char*)(bp)+WSIZE) 
#define LIST_NEXT_PTR(bp) ((char*)(bp))
#define LIST_PREV_BLK(bp) (GET(LIST_PREV_PTR(bp)))
#define LIST_NEXT_BLK(bp) (GET(LIST_NEXT_PTR(bp)))

#define ALLOC 1
#define UNALLOC 0

// mulit-level heap_list config
#define HEAP_LIST_NUM 1

// shortcut
// #define ALGIN(size) ()
#define SET_HEADER(bp, size, alloc)  (PUT(HDRP(bp), PACK(size, alloc)))
#define SET_FOOTER(bp, size, alloc)  (PUT(FTRP(bp), PACK(size, alloc)))
#define SET_TAIL_EXTENDED(bp) (PUT(HDRP(NEXT(bp)), PACK(0, ALLOC)))
#define SET_LIST_PREV(bp, prev)  (PUT(LIST_PREV_PTR(bp), prev))
#define SET_LIST_NEXT(bp, next)  (PUT(LIST_NEXT_PTR(bp), next))


/*internal function*/
// consistent function
// static int mm_init(); // initialize all config
static int mm_init_single_list(); // initialize single list to debug
static void *extend_heap(size_t words); // allocate memory from os and move the break
static void *coalesce_imme(void *bp); // coalesce immediately when free block
// static void *split(void *bp, size_t size); // split a block to two blocks
static void used(void *bp, size_t size); // occupy the block and set it used
static int is_valid_free(void *bp); // check if the bp is allocated and not out of bound
static void* my_sbrk(size_t size);
static void remove_from_list(void *bp); // remove the block from available list
static void insert_block_FIFO(void *bp);


// status variable
size_t data_size = 0;  // counting allocated data, avoid unnecessary time cost
size_t free_size = 0;  // counting available data, avoid nunecessary time cost
static char *heap_listp = NULL; // the start of heap pointer

// implement internal functions

// do size merge and set header, footer 
static void *coalesce_imme(void *bp){
  void *prev = PRED(bp);
  void *next = NEXT(bp);
  size_t cur_size = GET_BP_SIZE(bp);
  if(GET_BP_ALLOC(next) == UNALLOC){
    cur_size += GET_BP_SIZE(next);
    remove_from_list(next);
  }
  if(GET_BP_ALLOC(prev) == UNALLOC){
    cur_size += GET_BP_SIZE(prev);
    remove_from_list(prev);
    bp = prev;
  }
  SET_HEADER(bp, cur_size, UNALLOC);
  SET_FOOTER(bp, cur_size, UNALLOC);
  return bp;
}

static void *my_sbrk(size_t size){
  void *res = sbrk(size);
  if(res!= (void*)-1){
    data_size += size;
    free_size += size;
  }
  return res;
}

static int mm_init_single_list(){
  if((heap_listp = my_sbrk(4 * WSIZE))==(void*)-1){
    return -1;
  }
  // PUT(heap_listp, NULL); // start pointer 
  PUT(heap_listp+0*WSIZE, PACK(DSIZE, ALLOC));
  PUT(heap_listp+1*WSIZE, PACK(DSIZE, ALLOC));
  PUT(heap_listp+2*WSIZE, NULL); // next
  PUT(heap_listp+3*WSIZE,PACK(0, ALLOC));
  heap_listp += 2 * WSIZE;
  // heap_listp += 4 * WSIZE; // remove the heap_listp to the data start pointer
  return 0;
}

static void *extend_heap(size_t size){
  size_t asize = (size + DSIZE-1)/DSIZE * DSIZE;
  // size_t word = size / WSIZE;
  // size_t asize = words&1 ?(words+1)*WSIZE:words*WSIZE; 
  void *p;
  if((p == my_sbrk(asize)) == (void*)-1){
    perror("alloc error");
    return NULL;
  }
  // current block == tail + sbrk
  SET_HEADER(p, asize, UNALLOC);
  SET_FOOTER(p, asize, UNALLOC);
  SET_LIST_NEXT(p, NULL);
  SET_LIST_PREV(p, NULL);
  SET_TAIL_EXTENDED(p);
  // coalesce with previous segment
  p = coalesce_imme(p);
  insert_block_FIFO(p);
  return p;
}

static void insert_block_FIFO(void *p){
  if(!heap_listp){
    heap_listp = p;
    SET_LIST_NEXT(p, NULL);
  }
  else{
    SET_LIST_NEXT(p, LIST_NEXT_BLK(heap_listp));
    SET_LIST_PREV(p, heap_listp);
    SET_LIST_PREV(LIST_NEXT_BLK(heap_listp), p);
    SET_LIST_NEXT(heap_listp, p);
  }
}

static int is_valid_free(void *bp){
  if((heap_listp != NULL)&&(bp > heap_listp && bp < sbrk(0))&&(GET_BP_ALLOC(bp) == ALLOC)){
    return 0;
  }
  return -1;
}

static void remove_from_list(void *bp){
    void *prev = LIST_PREV_BLK(bp);
    void *next = LIST_NEXT_BLK(bp);
    if(prev){
          SET_LIST_NEXT(prev, next);
    }
    if(next){
        SET_LIST_PREV(next, prev);  
    }
    SET_LIST_PREV(bp, NULL);
    SET_LIST_NEXT(bp, NULL);
}

static void insert_list_after(void *bp, void *prev){
  if(prev == NULL){
    return; 
  }
  void *next = LIST_NEXT_BLK(prev);
  SET_LIST_PREV(bp, prev);
  SET_LIST_NEXT(bp, next);
  SET_LIST_NEXT(prev, bp);
  if(next){
    SET_LIST_PREV(next, bp);
  }
}

static void used(void *bp, size_t size){
  size_t remain = GET_BP_SIZE(bp) - size;
  if(remain >= MINSIZE){
    // need split the block
    void *rpt = bp + size + WSIZE;
    SET_HEADER(rpt, remain, UNALLOC);
    SET_FOOTER(rpt, remain, UNALLOC);
    insert_list_after(rpt, bp);
  }else{
    size = GET_BP_SIZE(bp);
  }
  remove_from_list(bp);
  SET_HEADER(bp, size, ALLOC);
  SET_FOOTER(bp, size, ALLOC);
  free_size -= size;
}

// implement user function

//First Fit malloc / free
void *ff_malloc(size_t size){
  if(!size) return NULL;
  if(!heap_listp) mm_init_single_list();
  size_t asize = MINSIZE;
  if(size > asize){
      asize = (size + WSIZE*2 + WSIZE - 1)/WSIZE * WSIZE;
  }
  void *cur = GET(heap_listp);
  int index = 0;
  /*if multiple heap */
  if(HEAP_LIST_NUM > 1){
    // get the start pointer of list based on size
  }
  void *bp;
  while(index < HEAP_LIST_NUM){
    while(cur!= NULL && GET_BP_SIZE(cur) < asize){
      cur = LIST_NEXT_BLK(cur);
    }
    index += 1;
  }
  if(cur != NULL){
    bp = cur;
  }else{
    // cannot find a suitable block, extend heap size
    bp = extend_heap(asize);
    if(bp == NULL){
      return NULL;
    }
  }
  used(bp, asize);
  return bp;
}


void ff_free(void *p){
  if(!is_valid_free(p)){
      perror("free invalid ptr");
      exit(EXIT_FAILURE);
  }
  free_size += GET_BP_SIZE(p);
  p = coalesce_imme(p);
  insert_block_FIFO(p);
}


void *bf_malloc(size_t size){
  if(!size) return NULL;
  if(!heap_listp) mm_init_single_list();
  size_t asize = MINSIZE;
  if(size > asize){
      asize = (size + WSIZE*2 + WSIZE - 1)/WSIZE * WSIZE;
  }
  void *cur = GET(heap_listp);
  int index = 0;
  /*if multiple heap */
  if(HEAP_LIST_NUM > 1){
    // get the start pointer of list based on size
  }
  void *bp = NULL;
  while(index < HEAP_LIST_NUM){
    while(cur!= NULL){
      if(GET_BP_SIZE(cur) < asize && (bp == NULL || GET_BP_SIZE(cur)-asize < GET_BP_SIZE(bp)-asize)){
        bp = cur;
      }
      cur = LIST_NEXT_BLK(cur);
    }
    index += 1;
  }
  if(bp == NULL){
    // cannot find a suitable block, extend heap size
    bp = extend_heap(asize);
    if(bp == NULL){
      return NULL;
    }
  }
  used(bp, asize);
  return bp;
}
void bf_free(void *ptr){
  // same as ff_free
  ff_free(ptr);
}

unsigned long get_data_segment_size(){
    return data_size;
}
unsigned long get_data_segment_free_space_size(){
    return free_size;
}


