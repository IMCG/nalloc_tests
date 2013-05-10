/**
 * @file   stack.c
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * @date   Sun Oct 14 04:37:31 2012
 * 
 * @brief  
 * 
 * 
 */

#define MODULE STACK

#include <stack.h>
#include <asm_util.h>
#include <global.h>

#define MAX_FAILURES 5

void stack_push(sanchor_t *anc, lfstack_t *stack){
    trace(anc, p);

    sanchor_t *top;
    int loops = 0;
    (void) loops;

    do{
        /* assert(loops++ <= MAX_FAILURES); */
        top = stack->top.ptr;
        anc->next = top;
    } while(cmpxchg64b((int64_t) anc,
                       (int64_t *) &stack->top.ptr,
                       (int64_t) top)
            != (int64_t) top);

    /* xadd(1, &stack->size); */
}

sanchor_t *stack_pop(lfstack_t *stack){
    trace(stack, p);

    tagptr_t old;
    tagptr_t new;
    int loops = 0;
    (void) loops;

    assert(aligned(&stack->top, 8));

    do{
        /* assert(loops++ <= MAX_FAILURES); */
        old = stack->top;
        if(old.ptr == NULL)
            return NULL;
        new.tag = old.tag + 1;
        /* Even if out of date, this should always be a readable ptr. */
        new.ptr = old.ptr->next;
    } while(stack->top.tag != old.tag ||
            cmpxchg128b(*(__int128_t*) &new,
                        (int128_t*) &stack->top,
                        *(int128_t*) &old)
            != *(int128_t*) &old);
                      
    return old.ptr;
}

sanchor_t *stack_pop_all(lfstack_t *stack){
    if(!stack->top.ptr)
        return NULL;
    return (sanchor_t *) xchg64b((int64_t) NULL, (int64_t *) &stack->top.ptr);
}
