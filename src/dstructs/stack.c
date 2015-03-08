#define MODULE STACKM

#include <stack.h>
#include <atomics.h>

cnt (lfstack_push)(sanchor *a, lfstack *s){
    assert(!a->n);
    for(lfstack x = *s;;){
        a->n = x.top;
        assert(x.size < ((uptr) 1 << (WORDBITS/2)) - 1);
        if(cas2_won(rup(x, .top=a, .size++), s, &x))
            return x.gen;
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

bool lfstack_empty(const lfstack *s){
    return !s->top;
}

uptr lfstack_gen(const lfstack *s){
    return s->gen;
}

cnt lfstack_size(const lfstack *s){
    return s->size;
}

lfstack (lfstack_push_iff)(sanchor *a, uptr gen, lfstack *s){
    for(lfstack x = *s;;){
        if(x.gen != gen)
            return x;
        a->n = x.top;
        if(cas2_won(rup(x, .top = a, .size++), s, &x))
            return x;
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
stack (lfstack_pop_all)(cnt incr, lfstack *s){
    for(lfstack x = *s;;){
        if((!x.top && !incr)
           || cas2_won(rup(x, .top=NULL, .gen += incr, .size=0), s, &x))
            return (stack){x.top, x.size};
    }
}

stack lfstack_pop_all_or_incr(cnt incr, lfstack *s){
    for(lfstack x = *s;;){
        if(!x.top){
            if(cas2_won(rup(x, .gen += incr), s, &x))
                return (stack){x.top, 0};
        }else if(cas2_won(rup(x, .top=NULL, .size=0), s, &x))
            return (stack){x.top, x.size};
    }
}

lfstack (lfstack_pop_all_iff)(uptr newg, lfstack *s, uptr oldg){
    for(lfstack x = *s;;){
        if(x.gen != oldg)
            return rup(x, .top=NULL);
        if((!x.top && !x.gen != oldg)
           || cas2_won(rup(x, .top=NULL, .gen = newg, .size=0), s, &x))
            return x;
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

sanchor *sanchor_next(sanchor *a){
    return a->n;
}

bool stack_empty(const stack *s){
    return !s->top;
}

cnt stack_size(const stack *s){
    return s->size;
}
