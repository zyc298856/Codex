#ifndef _UTIL_H
#define _UTIL_H
#include <stdint.h>
#include <string.h>

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/shm.h>
#include <fcntl.h>

#define         MIN(A, B)       ((A) < (B) ? (A) : (B))
#define         MAX(A, B)       ((A) > (B) ? (A) : (B))

void *align_malloc(int i_size );
void align_free(void *p );

int64_t mdate();
void msleep( int64_t delay );
#endif

