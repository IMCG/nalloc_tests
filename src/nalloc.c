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
#include <refcount.h>

#include <nalloc.h>
#include <global.h>

static ref_class_t wayward_bcache_ref_class = {
    .destructor = (void (*)(void*)) &wayward_bcache_free,
    .container_offset = offsetof(wayward_bcache_t, refs),
};

__attribute__ ((__aligned__(64)))
/* Cold slabs are those which have all blocks free and none of the are on the
   hot_blocks stack. */
static lfstack_t cold_slabs = FRESH_STACK;

/* DANGER: It happens to be the case that an uninitialized lfstack is all
   0. If that's not true, then you need a barrier during first time init
   (pthread_once won't suffice). */
__attribute__ ((__aligned__(64)))
/* TODO: Don't want hot_slabs */
/* Hot slabs are those which have blocks free the hot_blocks stack. */
static lfstack_t hot_slabs[NBSTACKS];

__attribute__ ((__aligned__(64)))
static __thread nalloc_profile_t profile;

/* Initialized in dynamic_init() */
static __thread simpstack_t slab_cache[NBSTACKS];
static __thread wayward_bcache_t *wayward_bcache;

/* Accounting info for pthread's wonky thread destructor setup. */
static pthread_once_t key_rdy = PTHREAD_ONCE_INIT;
static pthread_key_t destructor_key = 0;

void *malloc(size_t size){
    trace2(size, lu);
    if(size > MAX_BLOCK){
        large_block_t *large_found = large_alloc(size);
        /* profile_bytes(large_found->size); */
        return large_found ? large_found + 1 : NULL;
    }
    return alloc(size);
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
        return;
    }
    
    dealloc(buf);
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
        size_t old_size = slab_of(ptr)->block_size;
        memcpy(new, ptr, min(size, old_size));
    }
    free(ptr);
    return ptr;
}

static
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

static
void large_dealloc(large_block_t *block){
    assert(aligned(block, PAGE_SIZE));
    munmap(block, block->size);
}

static
int bcacheidx_of(size_t size){
    for(int i = 0; i < size; i++)
        if(size <= bcache_size_lookup[i])
            return i;
    LOGIC_ERROR("Little allocator vs big request: %lu.", size);
}

static
void *alloc(size_t size){
    trace2(size, lu);

    if(dynamic_init())
        return NULL;

    block_t *found;
    int cidx = bcacheidx_of(size);
    size_t rounded = bcache_size_lookup[cidx];
    
    slab_t *slab = lookup_sanchor(simpstack_peek(&slab_cache[cidx]),
                                  slab_t, sanc);
    if(!slab){
        found = alloc_from_wayward_blocks(cidx);
        if(found)
            goto done;
        slab = slab_install_new(rounded, cidx);
        if(!slab)
            return NULL;
    }
    
    found = alloc_from_slab(slab);
    if(!found)
        return NULL;
    
    if(slab_is_empty(slab))
        simpstack_pop(&slab_cache[cidx]);
    
done:
    assert(found);
    rassert(slab_of(found)->block_size, >=, size);
    assert(aligned(found, MIN_ALIGNMENT));
    return found;
}

static
block_t *alloc_from_wayward_blocks(int cidx){
    return
        stack_pop_lookup(block_t, sanc,
                         &wayward_bcache->bstacks[cidx]);
}

static
int slab_is_empty(slab_t *slab){
    return slab->hot_blocks.size == 0 && slab->nblocks_contig == 0;
}

/* A slab's hotness is indep. of its emptyness. */
static
int slab_is_hot(slab_t *slab){
    return slab->nblocks_contig == 0;
}

static 
unsigned int slab_compute_nblocks(size_t block_size){
    return (SLAB_SIZE - sizeof(slab_t)) / block_size;
}

static
void *alloc_from_slab(slab_t *slab){
    if(!slab_is_hot(slab))
        return &slab->blocks[slab->block_size * --slab->nblocks_contig];
    return simpstack_pop_lookup(block_t, sanc, &slab->hot_blocks);
}

static
slab_t *slab_install_new(size_t size, int sizeidx){
    slab_t *found = stack_pop_lookup(slab_t, sanc, &hot_slabs[sizeidx]);
    if(!found){
        found = stack_pop_lookup(slab_t, sanc, &cold_slabs);
        if(!found){
            found = mmap(NULL, SLAB_SIZE * SLAB_ALLOC_BATCH,
                         PROT_READ | PROT_WRITE,
                         MAP_POPULATE | MAP_PRIVATE | MAP_ANONYMOUS,
                         -1, 0);
            if(!found)
                return NULL;
            
            /* Note that we start from 1. */
            for(int i = 1; i < SLAB_ALLOC_BATCH; i++){
                slab_t *extra = (slab_t*) ((uptr_t) found + i * SLAB_SIZE);
                *extra = (slab_t) FRESH_SLAB;
                stack_push(&extra->sanc, &cold_slabs);
            }
            *found = (slab_t) FRESH_SLAB;
        }

        found->block_size = size;
        found->nblocks_contig = slab_compute_nblocks(size);
    }else
        assert(slab_is_hot(found));

    assert(found->block_size == size);

    profile_slabs(1);
    return found;
}

static
void slab_free(slab_t *freed, int cidx){
    assert(slab_is_hot(freed));
    /* Shouldn't be freeing slabs that have unfreed blocks that can go
       wayward. */
    freed->wayward_blocks = NULL;
    stack_push(&freed->sanc, &hot_slabs[cidx]);
    profile_slabs(-1);
}

static
void dealloc(block_t *b){
    trace2(b, p);

    slab_t *s = slab_of(b);
    int cidx = bcacheidx_of(s->block_size);

    if(s->wayward_blocks == wayward_bcache){
        simpstack_push(&b->sanc, &s->hot_blocks);
        if(slab_is_empty(s)){
            /* Slab becomes cold again. This has the advantage of making
               allocations sequential and thus cache and prefetch friendly. */
            s->nblocks_contig = slab_compute_nblocks(s->block_size);
            simpstack_push(&s->sanc, &slab_cache[cidx]);
        }
        /* TODO: release back to system. */
    }
    else
        return_wayward_block(b, s, cidx);
}

static
void return_wayward_block(block_t *b, slab_t *slab, int cidx){
    wayward_bcache_t *waycache = slab->wayward_blocks;
    if(waycache->type == HAS_ONE_BSTACK)
        stack_push(&b->sanc, &waycache->bstacks[0]);
    else if(waycache->type == HAS_MANY_BSTACKS){
        /* Why is it safe to take a ref here? Because the waycache won't be freed
           until all missing blocks are returned. */
        ref_up(&waycache->refs, 1, &wayward_bcache_ref_class);
        stack_push(&b->sanc,
                   &slab->wayward_blocks->bstacks[bcacheidx_of(slab->size)]);
        ref_down(&waycache->refs, 1, &wayward_bcache_ref_class);
    }
}

static
slab_t *slab_of(block_t *b){
    return (slab_t *) align_down_pow2(b, PAGE_SIZE);
}

/* Prototyped without args for compatibility with pthread_key_create. */
static
void free_slabs_atexit(){
    /* TODO: Obvious placeholder. */
    slab_t *cur;
    FOR_EACH_LLOOKUP(cur, slab_t, lanc, &priv_slabs)
        cur->wayward_blocks = &cur->disowned_blocks;
    ref_down(&wayward_bcache, 1, &wayward_bcache_refclass);
}

static
void first_time_init(void){
    pthread_key_create(&destructor_key, free_slabs_atexit);
};

static
int dynamic_init(void){
    if(!wayward_bcache){
        if(!wayward_bcache_install_new())
            return -1;
        pthread_once(&key_rdy, first_time_init);
        if(pthread_setspecific(destructor_key, (void*) !NULL))
            return -1;
        destructor_rdy = 1;
    }
    return 0;
}

static
int wayward_bcache_install_new(void){
    assert(!wayward_bcache);
    
    /* Are you ready for this? */
    static char unused_byte_of_memory;
    /* To prevent infinite recursion. */
    wayward_bcache = (big_wayward_bcache_t *) &unused_byte_of_memory;
    wayward_bcache = alloc(sizeof(big_wayward_bcache_t));
    if(!wayward_bcache)
        return -1;

    wayward_bcache->type_tag = HAS_MANY_BSTACKS;
    wayward_bcache->refs = INITIALIZED_REFCOUNT(1);
    for(int i = 0; i < NBSTACKS; i++)
        wayward_bcache->blocks[i] = INITIALIZED_STACK;
    
    slab_t *host_slab = slab_of(wayward_bcache);
    host_slab->wayward_blocks =
        &wayward_bcache->blocks[bcacheidx_of(host_slab)];
    /* /puts_on_sunglasses. */
}

/* Called via refcnt destructor. */
static
void wayward_bcache_free(wayward_bcache *bcache){
    for(int i = 0; i < bcache->nstacks; i++){
        block_t *wayward_block;
        FOR_EACH_SPOP_LOOKUP(block_t, sanc, &bcache->bstacks[i]){
            assert(slab_of(wayward_block)->wayward_blocks, ==,
                   &slab_of(wayward_block)->wayward_blocks bcache);
            dealloc(wayward_blocks);
        }
    }
    dealloc(bcache);
}

static
void profile_bytes(size_t nbytes){
    return;
    profile.num_bytes_highwater =
        max(profile.num_bytes += nbytes, profile.num_bytes_highwater);
}

static
void profile_slabs(size_t nslabs){
    return;
    profile.num_slabs_highwater =
        max(profile.num_slabs += nslabs, profile.num_slabs_highwater);
}

static
nalloc_profile_t *get_profile(void){
    return &profile;
}

static
int write_block_magics(block_t *b){
    if(!HEAP_DBG)
        return 1;
    for(int i = 0; i < (b->size - sizeof(*b)) / sizeof(*b->magics); i++)
        b->magics[i] = NALLOC_MAGIC_INT;
    return 1;
}

static
int block_magics_valid(block_t *b){
    if(!HEAP_DBG)
        return 1;
    for(int i = 0; i < (b->size - sizeof(*b)) / sizeof(*b->magics); i++)
        rassert(b->magics[i], ==, NALLOC_MAGIC_INT);
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
