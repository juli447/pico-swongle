#pragma once
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
typedef struct {pthread_mutex_t lock;pthread_cond_t data;unsigned head,tail,count,capacity;size_t size;unsigned char *items;} queue_t;
void queue_init(queue_t *,size_t,unsigned);
bool queue_try_remove(queue_t *,void *);
bool queue_try_add(queue_t *,const void *);
void queue_remove_blocking(queue_t *,void *);
