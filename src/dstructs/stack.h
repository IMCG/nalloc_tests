#pragma once

#include <peb_util.h>

typedef volatile struct sanchor{
    volatile struct sanchor *n;
} sanchor;
#define SANCHOR {}

typedef volatile struct stack{
    sanchor *top;
    cnt size;
} stack;
#define STACK {}
#define pudef (stack, "(stack){top:%,sz:%}", a->top, a->size)
#include <pudef.h>

sanchor *stack_pop(stack *s);
void stack_push(sanchor *a, stack *s);

align(sizeof(dptr))
typedef volatile struct lfstack{
    sanchor *top;
    struct{
        uptr gen:WORDBITS/2;
        uptr size:WORDBITS/2;
    };
} lfstack;
#define LFSTACK {}
#define pudef (lfstack, "(lfstack){top:%,sz:%}", a->top, a->size)
#include <pudef.h>

cnt lfstack_push(sanchor *a, lfstack *s);
sanchor *lfstack_pop(lfstack *s);
cnt lfstack_size(lfstack *s);
/* TODO: this should just return an lfstack. */
stack lfstack_pop_all(lfstack *s, cnt incr);

uptr lfstack_gen(lfstack *s);
cnt lfstack_size(lfstack *s);
uptr lfstack_push_iff(sanchor *a, uptr gen, lfstack *s);
sanchor *lfstack_pop_iff(uptr gen, uptr size, lfstack *s);

typedef volatile struct{
    uptr tag;
    sanchor *top;
} allstack;
#define ALLSTACK(tag) {(uptr) tag}

uptr allstack_push(sanchor *a, uptr n, allstack *s, uptr e);
allstack allstack_pop_all(uptr n, allstack *s, uptr e);

sanchor *sanchor_next(sanchor *a);

#define allstack_push(a, n, s, e)                                       \
    trace(STACKM, 1, allstack_push, a, PUN(uptr, n), s, PUN(uptr, e))
#define allstack_pop_all(n, s, e)                                       \
    trace(STACKM, 1, allstack_pop_all, PUN(uptr, n), s, PUN(uptr, e))

#define lfstack_push(as...) trace(STACKM, 1, lfstack_push, as)
#define lfstack_pop(as...) trace(STACKM, 1, lfstack_pop, as)
#define lfstack_pop_all(as...) trace(STACKM, 1, lfstack_pop_all, as)

#define lfstack_push_iff(as...) trace(STACKM, 1, lfstack_push_iff, as)

#define stack_pop(as...) trace(STACKM, 1, stack_pop, as)
#define stack_push(as...) trace(STACKM, 1, stack_push, as)

