/**
 * Lockfree slab allocator for type-stable memory and customizably local
 * caching of free memory, as well as plain malloc.
 *
 * Allocates lineages from slabs. A slab is a naturally aligned, contig
 * set of lineages of the same size, of the same type (id). A heritage is
 * a set of slabs of lineages of the same type. A lineage is a block of
 * memory except its contents are defined even after it's freed. It
 * represents all the generations of the same address that have kept the
 * same type.
 *
 * You can use linref_up(l, t) to prove that block l is of type t,
 * etc. The cool thing is that holding linrefs doesn't delay the free or
 * reallocation of l. In practice, it delays the freeing of its slab.
 *
 * The standard functions like malloc() use global heritages of
 * "polymorphic types" ("bullshit") of fixed sizes and no-op lin_init
 * functions.
 *
 * nalloc stores slab metadata away from slabs, in a huge BSS array. This
 * is the easiest way to make linref_up safe. Otherwise, you'd have to
 * forbid linref_up calls on pages not controlled by nalloc (think
 * memory-mapped files). That's not too onerous, but only if nalloc can't
 * return pages to the kernel. It also makes bigger-than-slab-size
 * allocations easy, and plays better with low-associativity caches by not
 * reserving the head or foot of each page for metadata.
 *
 * On the other hand, Linux programs like gdb can't handle a huge BSS, so
 * I restrict the dimensions of the heap. The array scheme is probably too
 * tough for Linux, but it wouldn't be very hard to switch back to
 * metadata in slab headers.
 *
 * TODO: slabs are actually never freed right now.
 *
 * TODO: local heritages need special cased code to avoid needless LOCKed
 * instructions. 
 *
 * TODO: need some heritage_destroy function, otherwise you have to leak
 * the slabs in local heritages upon thread death.
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
    cnt contig_blocks;
    heritage *volatile her;
    volatile struct align(sizeof(dptr)) tyx {
        type *t;
        iptr linrefs;
    } tx;
    lfstack hot_blocks;
} slabman;
#define SLAB {.free_blocks = STACK, .hot_blocks = LFSTACK}
#define pudef (slabman, "(slab){%, refs:%, contig:%, free:%, hot:%}",   \
               a->tx.t, a->tx.linrefs, a->contig_blocks,                \
               stack_size(&a->free_blocks),                             \
               lfstack_size(&a->hot_blocks))
#include <pudef.h>

typedef struct align(SLAB_SIZE) slab{
    u8 blocks[MAX_BLOCK];
}slab;

static slabman *slab_new(heritage *h);
static block *alloc_from_slab(slabman *s, heritage *h);
static err recover_hot_blocks(slabman *s);
static unsigned int slab_max_blocks(const slabman *s);
static bool slab_fully_hot(const slabman *s);
static slabman *slab_of(const block *b);
static u8 *blocks_of(const slabman *s);
static err write_magics(block *b, size bytes);         
static err magics_valid(block *b, size bytes);
static void slab_ref_down(slabman *s, lfstack *free_slabs);

#define slab_new(as...) trace(NALLOC, 2, slab_new, as)
#define slab_ref_down(as...) trace(NALLOC, LINREF_VERB, slab_ref_down, as)

lfstack shared_free_slabs = LFSTACK;

static slab *slabs = (slab *) HEAP_MIN;
static slabman slabmans[(HEAP_MAX - HEAP_MIN) / sizeof(slab)];

dbg iptr slabs_allocated;

dbg iptr slabs_used;
dbg cnt bytes_used;

#define PTYPE(s, ...) {#s, s, NULL, NULL, NULL}
static const type polytypes[] = { MAP(PTYPE, _,
        16, 32, 48, 64, 80, 96, 112, 128,
        192, 256, 384, 512, 1024, MAX_BLOCK)
};

#define PHERITAGE(i, ...)                                           \
    HERITAGE(&polytypes[i], 8, 2, new_slabs)
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

    void *b = h->owner &&  ? linalloc_local(h), linalloc_shared(h);
    if(!b)
        return NULL;

    assert(xadd(h->t->size, &bytes_used), 1);
    assert(b);
    assert(aligned_pow2(b, sizeof(dptr)));
}

void *(linalloc_local)(heritage *h){
    slabman *s = cof(stack_peek(&h->slabs), slabman, sanc);
    if(!s && !(s = slab_new(h)))
        return EOOR(), NULL;

    block *b = alloc_from_slab(s, h);
    if(slab_fully_hot(s) && recover_hot_blocks(s))
        if(s != cof(stack_pop(&s->sanc, &h->slabs), slabman, sanc))
            EWTF();
    return b;
}

void *(linalloc_shared)(heritage *h){
    slabman *s = cof(lfstack_pop(&h->slabs), slabman, sanc);
    if(!s && !(s = slab_new(h)))
        return EOOR(), NULL;

    block *b = alloc_from_slab(s, h);
    if(!slab_fully_hot(s) || !recover_hot_blocks(s))
        lfstack_push(&s->sanc, &h->slabs);
    
    return b;
}

static
block *(alloc_from_slab)(slabman *s, heritage *h){
    if(s->contig_blocks)
        return (block *) &blocks_of(s)[h->t->size * --s->contig_blocks];
    block *b = cof(stack_pop(&s->free_blocks), block, sanc);
    assert(b);
    return b;
}

static
bool slab_fully_hot(const slabman *s){
    return !(s->contig_blocks ||
             stack_size(&s->free_blocks));
}

static
err (recover_hot_blocks)(slabman *s){
    assert(!lfstack_gen(&s->hot_blocks));
    stack p = lfstack_pop_all_or_incr(1, &s->hot_blocks);
    if(!stack_size(&p))
        return -1;
    s->free_blocks = p;
    return 0;
}

static
slabman *(slab_new)(heritage *h){
    slabman *s = cof(lfstack_pop(h->free_slabs), slabman, sanc);
    if(!s){
        slab *sb = h->new_slabs(h->slab_alloc_batch);
        if(!sb)
            return NULL;
        assert(xadd(h->slab_alloc_batch, &slabs_allocated), 1);
        assert(aligned_pow2(sb, SLAB_SIZE));
        assert(sb >= (slab *) HEAP_MIN && sb < (slab *) HEAP_MAX);
        
        s = slab_of((block *) sb);
        *s = (slabman) SLAB;
        for(slabman *si = s + 1; si != &s[h->slab_alloc_batch]; si++){
            *si = (slabman) SLAB;
            lfstack_push(&si->sanc, h->free_slabs);
        }
    }
    assert(xadd(1, &slabs_used) >= 0);
    assert(!s->tx.linrefs);
    assert(!lfstack_size(&s->hot_blocks));
    
    s->her = h;
    if(s->tx.t != h->t){
        s->tx = (tyx){h->t};
        cnt nb = s->contig_blocks = slab_max_blocks(s);
        if(h->t->lin_init)
            for(cnt b = 0; b < nb; b++)
                h->t->lin_init((lineage *) &blocks_of(s)[b * h->t->size]);
        else
            for(cnt b = 0; b < nb; b++)
                assert(write_magics((block *) &blocks_of(s)[b * h->t->size],
                                    h->t->size));
    }
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

static bool fills_slab(cnt blocks, size bs){
    assert(blocks * bs <= SLAB_SIZE);
    return blocks * bs >= SLAB_SIZE - bs;
}

void (linfree)(lineage *l){
    block *b = l;
    *b = (block){SANCHOR};

    slabman *s = slab_of(b);
    assert(xadd(-s->tx.t->size, &bytes_used));

    if(lfstack_push(&b->sanc, &s->hot_blocks)){
        lfstack hb = lfstack_pop_all_iff(0, &s->hot_blocks, 1);
        if(!lfstack_empty(&hb)){
            assert(stack_empty(&s->free_blocks));
            /* TODO: a bit invasive */
            s->free_blocks = (stack){.top=hb.top, .size=hb.size};
            lfstack_push(&s->sanc, &s->her->slabs);
        }
    }
}

static
void (slab_ref_down)(slabman *s, lfstack *free_slabs){
    assert(s->tx.linrefs > 0);
    assert(s->tx.t);
    if(xadd((uptr) -1, &s->tx.linrefs) == 1){
        assert(xadd(-1, &slabs_used));
        lfstack_push(&s->sanc, free_slabs);
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
    slab_ref_down(s, s->her->free_slabs);
}

static
unsigned int slab_max_blocks(const slabman *s){
    return MAX_BLOCK / s->tx.t->size;
}

static constfun 
slabman *(slab_of)(const block *b){
    assert(b);
    return &slabmans[cof_aligned_pow2(b, slab) - slabs];
}

static constfun 
u8 *(blocks_of)(const slabman *s){
    return slabs[s - slabmans].blocks;
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

void nalloc_profile_report(void){
    ppl(0, slabs_allocated, slabs_used, bytes_used);
}

#pragma GCC visibility pop
