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

void stack_push(sanchor_t *anc, lfstack_t *stack){
    trace(anc, p, stack, p, stack->size, d);

    sanchor_t *top;

    do{
        top = stack->top.ptr;
        anc->next = top;
    } while(cmpxchg((int)anc, (int*)&stack->top.ptr, (int)top) != (int) top);

    xadd(1, &stack->size);
}

sanchor_t *stack_pop(lfstack_t *stack){
    trace(stack, p, stack->size, d);

    tagptr_t old;
    tagptr_t new;

    assert(aligned(&stack->top, 8));

    xadd(-1, &stack->size);

    do{
        old = stack->top;
        if(old.ptr == NULL){
            xadd(1, &stack->size);
            return NULL;
        }
        new.tag = old.tag + 1;
        /* Even if out of date, this should always be a readable kern ptr. */
        new.ptr = old.ptr->next;
        pause_randomly();
    } while(stack->top.tag != old.tag ||
            cmpxchg8b(*(int64_t*)&new, (int64_t*)&stack->top, *(int64_t*)&old)
            != *(int64_t*)&old);
                      
    return old.ptr;
}

int stack_size(lfstack_t *stack){
    return stack->size;
}
