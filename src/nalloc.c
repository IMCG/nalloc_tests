/**
 * @file   nalloc.c
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * @date   Sat Oct 27 19:16:19 2012
 * 
 * @brief A lockfree memory allocator.
 *
 * nalloc's main features are thread-local segregated list subheaps, a
 * lockfree global slab allocator to manage subheaps, and a lockfree
 * inter-thread memory transfer mechanism to return freed blocks to their
 * original subheap. Nah lock.
 *
 * Why are the stack reads safe:
 * - wayward_blocks. Only 1 thread pops. No one else can pop the top and then
 * force it to segfault.
 * - free_slabs. Safe simply because I never return to the system. Was going
 * to say it's because unmaps happen before insertion to stack, but it's still
 * possible to read top, then someone pops top, uses it, and unmaps it before
 * reinserting. Could honestly just do a simple array. 
 *
 */

#define MODULE NALLOC

#include <list.h>
#include <stack.h>
#include <sys/mman.h>

#include <nalloc.h>
#include <global.h>

__attribute__ ((__aligned__(64)))
static lfstack_t cold_slabs = INITIALIZED_STACK;

__attribute__ ((__aligned__(64)))
static lfstack_t hot_slabs = INITIALIZED_STACK;

__attribute__ ((__aligned__(64)))
static __thread nalloc_profile_t profile;

/* Initialized in dynamic_init() */
static __thread cache_t cache;

/* Accounting info for pthread's wonky thread destructor setup. */
static pthread_once_t key_rdy = PTHREAD_ONCE_INIT;
static pthread_key_t destructor_key = 0;

void *malloc(size_t size){
    trace2(size, lu);
    if(size > MAX_BLOCK){
        large_block_t *large_found = large_alloc(size);
        profile_bytes(large_found->size);
        return large_found ? large_found + 1 : NULL;
    }
    used_block_t *small_found = alloc(new_size);
    profile_bytes(small_found->size);
    return small_found ? small_found + 1 : NULL;
}

void free(void *buf){
    trace2(buf, p);
    if(!buf)
        return;
    /* TODO: Assumes no one generates pointers near page boundaries. Fine now,
       but won't cut it for large slabs. */
    if(aligned_pow2((large_block_t *) buf - 1, PAGE_SIZE)){
        large_block_t *large = (large_block_t *) buf - 1;
        profile_bytes(0 - large->size);
        large_dealloc(large);
    }
    else{
        profile_bytes(0 - small->size);
        dealloc(small);
    }
}

void *calloc(size_t nmemb, size_t size){
    void *found = malloc(nmemb * size);
    if(!found)
        return NULL;
    memset(found, 0, nmemb * size);
    return found;
}

void *realloc(void *ptr, size_t size){
    void *new = malloc(size);
    if(new){
        size_t old_size = ((used_block_t *)ptr - 1)->size;
        memcpy(new, ptr, min(size, old_size));
    }
    free(ptr);
    return ptr;
}

void *large_alloc(size_t size){
    size = align_up_pow2(size, PAGE_SIZE);
    large_block_t *found =
        mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
             -1, 0);
    if(!found)
        return NULL;
    found->size = size;
    return found;
}

void large_dealloc(large_block_t *block){
    assert(aligned(block, PAGE_SIZE));
    munmap(block, block->size);
}

int round_and_get_cacheidx(size_t *size){
    /* Probably unnecessary. But whatever. */
    size_t local = *size;
    for(int i = 0; i < *size; i++)
        if(local <= cache_size_lookup[i])
            return i;
    LOGIC_ERROR("Little allocator given big request.");
}

void *alloc(size_t size){
    trace2(size, lu);

    if(dynamic_init())
        return NULL;

    block_t *found;
    int cidx = round_and_get_cacheidx(&size);
    
    slab_t *slab = stack_peek_lookup(slab_t, sanc, &caches[cidx]);
    if(!slab){
        found = alloc_from_wayward_blocks(cidx);
        if(found)
            goto done;
        slab = slab_fetch(size, cidx);
        if(!slab)
            return NULL;
    }
    used_block_t *found = alloc_from_slab(slab);
    if(!found)
        return NULL;
    
    if(slab_is_empty(slab))
        simpstack_push(simpstack_pop(&caches[cidx)), &empty_slabs[cidx]);
    
done:
    assert(found);
    rassert(slab_of(found)->size, ==, size);
    assert(aligned(found->data, MIN_ALIGNMENT));
    return found;
}

used_block_t *alloc_from_wayward_blocks(int cidx){
    return
        stack_pop_lookup(block_t, sanc,
                         &wayward_block_cache->blocks[cidx]);
}

void *alloc_from_slab(slab_t *slab){
    if(slab->nblocks_contig)
        return slab->blocks[slab->size * nblocks_contig--];
    return stack_pop_lookup(block_t, sanc, &slab->blocks);
}

slab_t *slab_fetch(size_t size, int sizeidx){
    slab_t *fa = stack_pop_lookup(slab_t, sanc, &hot_slabs[sizeidx]);
    if(!fa){
        fa = stack_pop_lookup(slab_t, sanc, &cold_slabs);
        if(!fa){
            fa = mmap(NULL, SLAB_SIZE * SLAB_ALLOC_BATCH,
                      PROT_READ | PROT_WRITE, MAP_POPULATE |
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if(!fa)
                return NULL;
            /* Note that we start from 1. */
            for(int i = 1; i < SLAB_ALLOC_BATCH; i++){
                slab_t *extra = (void*) ((uptr_t) fa + i * SLAB_SIZE);
                *extra = (slab_t) INITIALIZED_SLAB;
                stack_push(&extra->sanc, &cold_slabs);
            }
        }

        *fa = (slab_t) INITIALIZED_SLAB;
        fa->size = size;
    }

    /* TODO: What about postmortem blocks. */
    
    profile_slabs(1);
    
    return fa;
}

void slab_free(slab_t *freed){
    stack_push(&freed->sanc, &hot_slabs);
    profile_slabs(-1);
}

void dealloc(block_t *b){
    trace2(_b, p);

    slab_t *s = slab_of(b);
    int cidx = cacheidx_of(s->size);

    /* TODO: Must get priv_wayward_cache set up. */
    if(s->wayward_blocks == &wayward_cache->blocks[cidx]){
        simp_push(&b->sanc, &s->blocks);
        if(b->is_empty)
            list_remove(&b->lanc, &s->blocks);
        /* TODO: release back to system. */
    }
    else
        return_wayward_block(b, s, cidx);
}

void return_wayward_block(wayward_block_t *b, slab_t *slab){
    stack_push(&b->sanc, slab->wayward_blocks);

    if(slab->stack.size >= slab_max_blocks(slab))
        /* ref_down(...etc); */
}

slab_t *slab_of(block_t *b){
    return (slab_t *) align_down_pow2(b, PAGE_SIZE);
}

void free_slabs_atexit(){
    /* TODO: Obvious placeholder. */
    slab_t *cur;
    FOR_EACH_LLOOKUP(cur, slab_t, lanc, &priv_slabs)
        cur->wayward_blocks = &cur->disowned_blocks;
}

void jump_through_hoop(void){
    pthread_key_create(&destructor_key, free_slabs_atexit);
};

int dynamic_init(void){
    if(!cache){
        cache = cache_new();
        if(!cache)
            return -1;
        pthread_once(&key_rdy, jump_through_hoop);
        if(pthread_setspecific(destructor_key, (void*) !NULL))
            return -1;
        destructor_rdy = 1;
    }
    return 0;
}


void profile_bytes(size_t nbytes){
    profile.num_bytes_highwater =
        max(profile.num_bytes += nbytes, profile.num_bytes_highwater);
}

void profile_slabs(size_t nslabs){
    profile.num_slabs_highwater =
        max(profile.num_slabs += nslabs, profile.num_slabs_highwater);
}

nalloc_profile_t *get_profile(void){
    return &profile;
}

int free_block_valid(block_t *b){
    assert(block_magics_valid(b));
    assert(b->size >= MIN_BLOCK);
    assert(b->size <= MAX_BLOCK);
    assert(!l_neighbor(b) || l_neighbor(b)->size == b->l_size);
    assert(!r_neighbor(b) || r_neighbor(b)->l_size == b->size);
    assert(lanchor_valid(&b->lanc,
                         &blist_smaller_than(b->size)->blocks));
    
    return 1;
}

int used_block_valid(used_block_t *_b){
    block_t *b = (block_t *) _b;
    assert(b);
    assert(b->size >= MIN_BLOCK);
    assert(b->size <= MAX_BLOCK);
    assert(aligned(_b->data, MIN_ALIGNMENT));
    /* These reads aren't actually thread safe, as the neighbors may be
       splitting in another thread. Unless we own the slab, in which case the
       only splitter is us. */
    if(slab_of(b)->wayward_blocks == &priv_wayward_blocks){
        assert(!l_neighbor(b) || l_neighbor(b)->size == b->l_size);
        assert(!r_neighbor(b) || r_neighbor(b)->l_size == b->size);
    }
    
    return 1;
}

int write_block_magics(block_t *b){
    if(!HEAP_DBG)
        return 1;
    for(int i = 0; i < (b->size - sizeof(*b)) / sizeof(*b->magics); i++)
        b->magics[i] = NALLOC_MAGIC_INT;
    return 1;
}

int block_magics_valid(block_t *b){
    if(!HEAP_DBG)
        return 1;
    for(int i = 0; i < (b->size - sizeof(*b)) / sizeof(*b->magics); i++)
        rassert(b->magics[i], ==, NALLOC_MAGIC_INT);
    return 1;
}

int free_slab_valid(slab_t *a){
    /* assert(sanchor_valid(&a->sanc)); */
    return 1;
}

int used_slab_valid(slab_t *a){
    return 1;
}

void posix_memalign(){
    UNIMPLEMENTED();
}

void aligned_alloc(){
    UNIMPLEMENTED();
}

void *valloc(){
    UNIMPLEMENTED();
}

void *memalign(){
    UNIMPLEMENTED();
}

void *pvalloc(){
    UNIMPLEMENTED();
}
