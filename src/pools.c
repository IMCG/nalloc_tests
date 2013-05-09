/**
 * @file   pools.c
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * @date   Sun Oct  7 04:45:07 2012
 * 
 * @brief  A lockfree cache of identically sized blocks of free memory.
 * 
 */

#define MODULE POOLS

#include <pools.h>
#include <asm_util.h>
#include <stack.h>

#include <global.h>

pool_t page_pool = INITIALIZED_POOL();

void *pool_get(pool_t *pool){
    trace(pool, p, pool->blocks.size, d);
    if(!ENABLE_POOLS)
        return NULL;
    return stack_pop(&pool->blocks);
}

int pool_put(void *block, pool_t *pool){
    trace(block, p, pool, p, pool->blocks.size, d);
    if(!ENABLE_POOLS)
        return -1;
    stack_push((sanchor_t *)block, &pool->blocks);
    return 0;
}
        
