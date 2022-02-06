
#ifndef __MY_MALLOC__
#define __MY_MALLOC__
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
/*function for user*/
//Thread Safe malloc/free: locking version 
void *ts_malloc_lock(size_t size); 
void ts_free_lock(void *ptr);

//Thread Safe malloc/free: non-locking version 
void *ts_malloc_nolock(size_t size); 
void ts_free_nolock(void *ptr);

#endif