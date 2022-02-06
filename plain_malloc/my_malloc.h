#ifndef __MY_MALLOC__
#define __MY_MALLOC__

#include <stdio.h>
#include <stdlib.h>
/*function for user*/
//First Fit malloc / free
void *ff_malloc(size_t size);
void ff_free(void *ptr);

// Best Fit malloc / free
void *bf_malloc(size_t size);
void bf_free(void *ptr);

unsigned long get_data_segment_size(); //in bytes
unsigned long get_data_segment_free_space_size(); //in bytes


#endif
