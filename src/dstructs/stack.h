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
cnt stack_size(const stack *s);
bool stack_empty(const stack *s);

/* TODO: too few bits for gen. I can actually make do without a precise
   size, but I decided to keep it around temporarily, for debugging. */
align(sizeof(dptr))
typedef volatile struct lfstack{
    sanchor *top;
    struct{
        uptr size:WORDBITS/2;
        uptr gen:WORDBITS/2;
    };
} lfstack;
#define LFSTACK {}
#define pudef (lfstack, "(lfstack){top:%,sz:%}", a->top, a->size)
#include <pudef.h>

uptr lfstack_push(sanchor *a, lfstack *s);
sanchor *lfstack_pop(lfstack *s);
stack lfstack_pop_all(cnt incr, lfstack *s);

uptr lfstack_gen(const lfstack *s);
cnt lfstack_size(const lfstack *s);
bool lfstack_empty(const lfstack *s);

lfstack lfstack_push_iff(sanchor *a, uptr gen, lfstack *s);
sanchor *lfstack_pop_iff(uptr gen, uptr size, lfstack *s);
stack lfstack_pop_all_or_incr(cnt incr, lfstack *s);
lfstack lfstack_pop_all_iff(uptr newg, lfstack *s, uptr oldg);

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

