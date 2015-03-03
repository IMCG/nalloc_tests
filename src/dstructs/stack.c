#define MODULE STACKM

#include <stack.h>
#include <atomics.h>

cnt (lfstack_push)(sanchor *a, lfstack *s){
    assert(!a->n);
    for(lfstack x = *s;;){
        a->n = x.top;
        assert(x.size < ((uptr) 1 << (WORDBITS/2)) - 1);
        if(cas2_won(rup(x, .top=a, .size++), s, &x))
            return x.size;
    }
}

sanchor *(lfstack_pop)(lfstack *s){
    for(lfstack x = *s;;){
        if(!x.top)
            return NULL;
        if(cas2_won(rup(x, .top=x.top->n, .gen++, .size--), s, &x)){
            x.top->n = NULL;
            return x.top;
        }
    }
}

uptr lfstack_gen(lfstack *s){
    return s->gen;
}

cnt lfstack_size(lfstack *s){
    return s->size;
}

uptr (lfstack_push_iff)(sanchor *a, uptr gen, lfstack *s){
    for(lfstack x = *s;;){
        if(x.gen != gen)
            return x.gen;
        a->n = x.top;
        if(cas2_won(rup(x, .top = a, .size++), s, &x))
            return gen;
    }
}

sanchor *(lfstack_pop_iff)(uptr gen, uptr size, lfstack *s){
    for(lfstack x = *s;;){
        if(!x.top || x.gen != gen || x.size != size)
            return NULL;
        if(cas2_won(rup(x, .top=x.top->n, .gen++, .size--), s, &x)){
            x.top->n = NULL;
            return x.top;
        }
    }
}

/* Callers can avoid gen updates if they only ever pop_all from s, or can
   arrange for it to communicate something extra. */
stack (lfstack_pop_all)(lfstack *s, cnt incr){
    for(lfstack x = *s;;){
        if((!x.top && !incr)
           || cas2_won(rup(x, .top=NULL, .gen += incr, .size=0), s, &x))
            return (stack){x.top, x.size};
    }
}

void (stack_push)(sanchor *a, stack *s){
    assert(!a->n);
    assert(a != s->top);
    
    a->n = s->top;
    s->top = a;
    s->size++;
}

sanchor *(stack_pop)(stack *s){
    sanchor *t = s->top;
    if(!t)
        return NULL;
    s->top = t->n;
    s->size--;
    
    t->n = NULL;
    return t;
}

uptr (allstack_push)(sanchor *a, uptr n, allstack *s, uptr e){
    for(allstack t = *s;;){
        if(t.tag != e)
            return t.tag;
        a->n = t.top;
        if(cas2_won(((allstack){n, a}), s, &t))
            return 0;
    }
}

allstack (allstack_pop_all)(uptr n, allstack *s, uptr e){
    for(allstack t = *s;;)
        if(t.tag != e || cas2_won(((allstack){n, NULL}), s, &t))
            return t;
}

sanchor *sanchor_next(sanchor *s){
    return s->n;
}

