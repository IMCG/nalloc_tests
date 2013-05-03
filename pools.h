/**
 * @file   pools.h
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * @date   Sun Oct  7 04:47:59 2012
 * 
 * @brief  
 * 
 * 
 */

#ifndef PEB_POOL_H
#define PEB_POOL_H

#include <stdint.h>
#include <stack.h>

typedef struct{
    lfstack_t blocks;
} pool_t;

#define INITIALIZED_POOL(SIZE)                  \
    {.blocks = INITIALIZED_STACK}

extern pool_t page_pool;

void *pool_get(pool_t *pool);
int pool_put(void *block, pool_t *pool);

#endif
