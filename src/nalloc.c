/**
 * Lockfree slab allocator that can provide type-stable memory and
 * customizably local caching of free blocks, or just do malloc.
 *
 * A lineage L is a block of memory s.t there's a type T s.t. if a call to
 * linref_up(&L, &T) == 0 and no corresponding call to linref_down has yet
 * been made, then:
 * - There's a heritage H s.t. only linalloc(&H) may return &L, even if
 * linfree(&L) is called.
 * - T is unqiue.
 * - No function in this file will ever write to any part of L except for
 * its sizeof(lineage)-byte header.
 *
 * A heritage H is a cache of lineages with associated type T s.t., if
 * linalloc(&H) == L and no call to linfree(L) has been made, then
 * linref_up(&L, &T) == 0. Also, H has a function block_init s.t. for all
 * L, block_init(L) is only called the first time that linalloc(&H)
 * returns L.
 *
 * Lineages provide "type-stable" memory because, if you get a linref on a
 * lineage L, then you've associated L with a type T s.t. only calls to
 * linalloc on heritages also associated with T could return L. Combined
 * with the no-write promise, this lets you safely free and reallocate L
 * without losing guarantees about the validity of its contents, but only
 * into the limited subset of free blocks wich are associated with T.
 *
 * malloc() and co are wrappers around linalloc and linfree, using
 * heritages keyed to "polymorphic types" of fixed sizes (bullshit) and
 * no-op block_init functions.
 *
 * TODO: some heritage_destroy function is needed. Doing nothing upon
 * thread death doesn't break anything, but it leaks the _full_ slabs in
 * all thread-local heritages.
 *
 * TODO!!!: blocks must be linited before linref_up can return true. Then
 * linref_up provides guarantee that the only writes that occurred in
 * nalloc.c were to the block header and via linit.
 */

#define MODULE NALLOC

#include <stack.h>
#include <list.h>
#include <nalloc.h>
#include <atomics.h>
#include <thread.h>

/* Prevents system library functions from using nalloc. Useful for
   debugging. */
#pragma GCC visibility push(hidden)

#define HEAP_DBG 0
#define LINREF_ACCOUNT_DBG 1
#define NALLOC_MAGIC_INT 0x01FA110C
#define LINREF_VERB 2

typedef struct tyx tyx;

typedef volatile struct slabman{
    sanchor sanc;
    stack free_blocks;
    cnt cold_blocks;
    void *owner;
    struct heritage *her;
    lfstack *hot_slabs;
    struct align(sizeof(dptr)) tyx {
        type *t;
        iptr linrefs;
    } tx;
    lfstack wayward_blocks;
} slabman;
#define SLAB {.free_blocks = STACK, .wayward_blocks = LFSTACK}
#define pudef (slabman, "(slab){%, refs:%, o:%, lfree:%, wfree:%}",     \
               a->tx.t, a->tx.linrefs, a->owner, a->free_blocks.size,   \
               a->wayward_blocks.size)
#include <pudef.h>

typedef struct align(SLAB_SIZE) slab{
    u8 blocks[MAX_BLOCK];
}slab;

static slabman *slab_new(heritage *h);
static block *alloc_from_slab(slabman *s, heritage *h);
static void dealloc_from_slab(block *b, slabman *s);
static unsigned int slab_max_blocks(const slabman *s);
static cnt slab_num_priv_blocks(const slabman *s);
static slabman *slab_of(const block *b);
static u8 *blocks_of(const slabman *s);
static err write_magics(block *b, size bytes);         
static err magics_valid(block *b, size bytes);
static void slab_ref_down(slabman *s, lfstack *hot_slabs);

#define slab_new(as...) trace(NALLOC, 2, slab_new, as)
#define slab_ref_down(as...) trace(NALLOC, LINREF_VERB, slab_ref_down, as)

lfstack hot_slabs = LFSTACK;

/* slabmans creates enough BSS to mandate -mcmodel=medium. I'd like to
   ignore slabs lower than &end, but gcc constant expression rules would
   forbid aligning &end or computing the size of slabmans. */
static slab *slabs = (slab *) 0;
/* slabman *slabmans = NULL; */
/* static slabman slabmans[((uptr) 0 - sizeof(slab))/sizeof(slab)]; */
static slabman slabmans[(HEAP_MAX - HEAP_MIN) / sizeof(slab)];

dbg iptr slabs_used;
dbg cnt bytes_used;

#define PTYPE(s, ...) {#s, s, NULL, NULL, NULL}
static const type polytypes[] = { MAP(PTYPE, _,
        16, 32, 48, 64, 80, 96, 112, 128,
        192, 256, 384, 512, 1024, MAX_BLOCK)
};

#define PHERITAGE(i, ...)                                           \
    HERITAGE(&polytypes[i], 8, 1, new_slabs)
static heritage poly_heritages[] = {
    ITERATE(PHERITAGE, _, 14)
};

static
heritage *poly_heritage_of(size size){
    for(uint i = 0; i < ARR_LEN(polytypes); i++)
        if(polytypes[i].size >= size)
            return &poly_heritages[i];
    EWTF();
}

void *(malloc)(size size){
    if(!size)
        return TODO(), NULL;
    if(size > MAX_BLOCK)
        return TODO(), NULL;
    block *b = (linalloc)(poly_heritage_of(size));
    if(b)
        assert(magics_valid(b, poly_heritage_of(size)->t->size));
    return b;
}

void *(linalloc)(heritage *h){
    if(poisoned())
        return NULL;
    
    slabman *s = cof(lfstack_pop(&h->slabs), slabman, sanc);
    if(!s && !(s = slab_new(h)))
        return EOOR(), NULL;

    /* assert(aligned_pow2(s, SLAB_SIZE)); */
    assert(!s->owner);
    block *b = alloc_from_slab(s, h);
    if(slab_num_priv_blocks(s))
        lfstack_push(&s->sanc, &h->slabs);
    else{
        s->owner = this_thread();
        s->her = h;
    }

    assert(xadd(h->t->size, &bytes_used), 1);
    
    assert(b);
    assert(aligned_pow2(b, sizeof(dptr)));
    return b;
}

static
block *(alloc_from_slab)(slabman *s, heritage *h){
    block *b;
    if(s->cold_blocks){
        b = (block *) &blocks_of(s)[h->t->size * --s->cold_blocks];
        assert(magics_valid(b, h->t->size));
        return b;
    }
    b = cof(stack_pop(&s->free_blocks), block, sanc);
    if(b)
        return b;
    s->free_blocks = lfstack_pop_all(&s->wayward_blocks, 1);
    return cof(stack_pop(&s->free_blocks), block, sanc);
}

static
slabman *(slab_new)(heritage *h){
    slabman *s = cof(lfstack_pop(h->hot_slabs), slabman, sanc);
    if(!s){
        slab *sb = h->new_slabs(h->slab_alloc_batch);
        if(!sb)
            return NULL;
        assert(aligned_pow2(sb, SLAB_SIZE));
        s = slab_of((block *) sb);
        for(slabman *si = s; si != &s[h->slab_alloc_batch]; si++){
            *si = (slabman) SLAB;
            if(si != s)
                lfstack_push(&si->sanc, h->hot_slabs);
        }
    }
    assert(xadd(1, &slabs_used) >= 0);
    assert(!s->tx.linrefs && !s->owner);
    assert(!s->wayward_blocks.size);
    
    s->her = h;
    if(s->tx.t != h->t){
        s->tx = (tyx){h->t};
        cnt mb = s->cold_blocks = slab_max_blocks(s);
        if(h->t->lin_init)
            for(cnt b = 0; b < mb; b++)
                h->t->lin_init((lineage *) &blocks_of(s)[b * h->t->size]);
        else
            for(uint i = 0; i < s->cold_blocks; i++)
                assert(write_magics((block *) &blocks_of(s)[i * h->t->size],
                                    h->t->size));
    }
    
    s->hot_slabs = h->hot_slabs;    
    s->tx.linrefs = 1;
    return s;
}

void (free)(void *b){
    lineage *l = (lineage *) b;
    if(!b)
        return;
    assert(write_magics(l, slab_of(l)->tx.t->size));
    (linfree)(l);
}
void (linfree)(lineage *l){
    block *b = l;
    slabman *s = slab_of(b);
    *b = (block){SANCHOR};

    assert(s->tx.t);
    assert(xadd(-s->tx.t->size, &bytes_used));

    heritage *h = s->her;
    if(s->owner == this_thread() && h->slabs.size < h->max_slabs){
        /* TODO: yeah, you own it till you free 1 block. Not so
           great. More complicated scheme could ensure that it's not lost
           if you don't push it upon first. Consider
           lfstack_push_if(size_is,..)*/
        s->owner = NULL;
        dealloc_from_slab(b, s);
        lfstack_push(&s->sanc, &h->slabs);
    }else{
        int nwb = lfstack_push(&b->sanc, &s->wayward_blocks);
        if(&blocks_of(s)[s->tx.t->size * (nwb + 2)] >
           &blocks_of(s)[MAX_BLOCK])
        {
            assert(!s->free_blocks.size);
            s->cold_blocks = s->wayward_blocks.size;
            s->wayward_blocks = (lfstack) LFSTACK;
            slab_ref_down(s, s->hot_slabs);
        }
    }
}

static
void (dealloc_from_slab)(block *b, slabman *s){
    stack_push(&b->sanc, &s->free_blocks);
}

static
void (slab_ref_down)(slabman *s, lfstack *hot_slabs){
    assert(s->tx.linrefs > 0);
    assert(s->tx.t);
    if(xadd((uptr) -1, &s->tx.linrefs) == 1){
        assert(xadd(-1, &slabs_used));
        s->owner = NULL;
        s->her = NULL;
        lfstack_push(&s->sanc, hot_slabs);
    }
}

err (linref_up)(volatile void *l, type *t){
    assert(l);
    if(l < heap_start() || l > heap_end())
        return EARG();
    
    slabman *s = slab_of((void *) l);
    for(tyx tx = s->tx;;){
        if(tx.t != t || !tx.linrefs)
            return EARG("Wrong type.");
        log(LINREF_VERB, "linref up! % % %", l, t, tx.linrefs);
        assert(tx.linrefs > 0);
        if(cas2_won(((tyx){t, tx.linrefs + 1}), &s->tx, &tx))
            return T->nallocin.linrefs_held++, 0;
    }
}

void (linref_down)(volatile void *l){
    T->nallocin.linrefs_held--;
    slabman *s = slab_of((void *) l);
    slab_ref_down(s, s->hot_slabs);
}

static
unsigned int slab_max_blocks(const slabman *s){
    return MAX_BLOCK / s->tx.t->size;
}

static
cnt slab_num_priv_blocks(const slabman *s){
    return s->free_blocks.size + s->cold_blocks;
}

static constfun 
slabman *(slab_of)(const block *b){
    return &slabmans[cof_aligned_pow2(b, slab) - slabs];
}

static constfun 
u8 *(blocks_of)(const slabman *b){
    return slabs[b - slabmans].blocks;
}

void *(smalloc)(size size){
    return (malloc)(size);
};

void (sfree)(void *b, size size){
    assert(slab_of(b)->tx.t->size >= size);
    (free)(b);
}

void *(calloc)(size nb, size bs){
    u8 *b = (malloc)(nb * bs);
    if(b)
        memset(b, 0, nb * bs);
    return b;
}

void *(realloc)(void *o, size size){
    u8 *b = (malloc)(size);
    if(b && o)
        memcpy(b, o, size);
    free(o);
    return b;
}

static
int write_magics(block *b, size bytes){
    if(!HEAP_DBG)
        return 1;
    int *magics = (int *) (b + 1);
    for(size i = 0; i < (bytes - sizeof(*b))/sizeof(*magics); i++)
        magics[i] = NALLOC_MAGIC_INT;
    return 1;
}

static
int magics_valid(block *b, size bytes){
    if(!HEAP_DBG)
        return 1;
    int *magics = (int *) (b + 1);
    for(size i = 0; i < (bytes - sizeof(*b))/sizeof(*magics); i++)
        assert(magics[i] == NALLOC_MAGIC_INT);
    return 1;
}

err fake_linref_up(void){
    T->nallocin.linrefs_held++;
    return 0;
}

void fake_linref_down(void){
    T->nallocin.linrefs_held--;
}

void linref_account_open(linref_account *a){
    assert(a->baseline = T->nallocin.linrefs_held, 1);
}

void linref_account_close(linref_account *a){
    if(LINREF_ACCOUNT_DBG)
        assert(T->nallocin.linrefs_held == a->baseline);
}

void byte_account_open(byte_account *a){
    assert(a->baseline = bytes_used, 1);
}

void byte_account_close(byte_account *a){
    assert(a->baseline == bytes_used);
}

void *memalign(size align, size sz){
    EWTF();
    assert(sz <= MAX_BLOCK
           && align < PAGE_SIZE
           && align * (sz / align) == align);
    if(!is_pow2(align) || align < sizeof(void *))
        return NULL;
    return malloc(sz);
}
int posix_memalign(void **mptr, size align, size sz){
    return (*mptr = memalign(align, sz)) ? 0 : -1;
}
void *pvalloc(size sz){
    EWTF();
}
void *aligned_alloc(size align, size sz){
    return memalign(align, sz);
}
void *valloc(size sz){
    EWTF();
}

#pragma GCC visibility pop
