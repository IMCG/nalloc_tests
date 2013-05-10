/**
 * @file   stack.h
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * @date   Fri Oct 19 02:07:12 2012
 * 
 * @brief  
 * 
 */

#ifndef STACK_H
#define STACK_H

#include <peb_macros.h>

typedef struct sanchor_t{
    struct sanchor_t *next;
} sanchor_t;

#define INITIALIZED_SANCHOR { .next = NULL }

typedef struct{
    int64_t tag;
    sanchor_t *ptr;
} tagptr_t;

COMPILE_ASSERT(sizeof(tagptr_t) == 16);

typedef struct{
    tagptr_t top __attribute__((__aligned__ (16)));
}lfstack_t;

#define INITIALIZED_STACK                       \
    {                                           \
        .top = {0, NULL},                       \
    }

#define lookup_sanchor(ptr, container_type, field)    \
    container_of(ptr, container_type, field)          

#define stack_pop_lookup(container_type, field, stack)      \
    lookup_sanchor(stack_pop(stack), container_type, field) 

#define FOR_EACH_SPOP_LOOKUP(cur_struct, struct_type, field_name, stack)\
    for(                                                                \
        cur_struct = stack_pop_lookup(struct_type, field_name, stack);  \
        cur_struct != NULL;                                             \
        cur_struct = stack_pop_lookup(struct_type, field_name, stack)   \
        )                                                               \

#define FOR_EACH_SPOPALL_LOOKUP(cur_struct, tmp, struct_type, field_name, stack) \
    for(                                                                \
        ((cur_struct = lookup_sanchor(stack_pop_all(stack),             \
                                      struct_type,                      \
                                      field_name)) != NULL)             \
        && (printf("%p", cur_struct),                                   \
            (tmp = lookup_sanchor(cur_struct->field_name.next,          \
                                 struct_type,                           \
                                  field_name)));                        \
        cur_struct != NULL;                                             \
        cur_struct = tmp                                                \
        )                                                               \


void stack_push(sanchor_t *anc, lfstack_t *stack);
sanchor_t *stack_pop(lfstack_t *stack);
sanchor_t *stack_pop_all(lfstack_t *stack);

#endif
