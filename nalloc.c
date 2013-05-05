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
 * - free_arenas. Safe simply because I never return to the system. Was going
 * to say it's because unmaps happen before insertion to stack, but it's still
 * possible to read top, then someone pops top, uses it, and unmaps it before
 * reinserting. Could honestly just do a simple array. 
 *
 */

#define MODULE NALLOC

#include <nalloc.h>
#include <list.h>
#include <stack.h>
#include <sys/mman.h>
#include <global.h>

#define MAX_ARENA_FAILS 10

static lfstack_t free_arenas = INITIALIZED_STACK;
__thread list_t private_arenas = INITIALIZED_LIST;
/* Warning: It happens to be the case that INITIALIZED_BLIST is a big fat
   NULL. */
__thread blist_t blists[NBLISTS];
__thread block_t dummy = { .free = 0, .size = MAX_BLOCK, .l_size = MAX_BLOCK };

void *_nmalloc(size_t size){
    trace2(size, lu);
    used_block_t *found = alloc(ualign_up(size + sizeof(used_block_t),
                                          MIN_BLOCK),
                                MIN_ALIGNMENT);
    return found ? found + 1 : NULL;
}

/* void *_nmemalign(size_t alignment, size_t size){ */
/*     trace2(alignment, lu, size, lu); */
/*     used_block_t *found = alloc(size + sizeof(used_block_t), alignment); */
/*     return found ? found + 1 : NULL; */
/* } */

/* void *_ncalloc(size_t nelt, size_t eltsize){ */
/*     trace2(nelt, lu, eltsize, lu); */
/*     uint8_t *new = _nmalloc(nelt * eltsize); */
/*     if(new) */
/*         for(int i = 0; i < nelt * eltsize; i++) */
/*             new[i] = 0; */
/*     return new; */
/* } */

/* void *_nrealloc(void *buf, size_t new_size){ */
/*     trace2(buf, p, new_size, lu); */
/*     void *new = _nmalloc(new_size); */
/*     if(new && buf){ */
/*         memcpy(new, buf, ((used_block_t *) buf - 1)->size); */
/*         _nfree(buf); */
/*     } */
/*     return new; */
/* } */
        
void _nfree(void *buf){
    trace2(buf, p);
    dealloc((used_block_t *) buf - 1);
}

/* TODO: Obviously... */
void *_nsmalloc(size_t size){
    trace2(size, lu);
    if(size == PAGE_SIZE)
        return arena_new();
    return _nmalloc(size);
}

/* void *_nsmemalign(size_t alignment, size_t size){ */
/*     trace2(alignment, lu, size, lu); */
/*     if(size == PAGE_SIZE && aligned(alignment, PAGE_SIZE)) */
/*         return arena_new(); */
/*     return _nmemalign(size, alignment); */
/* } */

void _nsfree(void *buf, size_t size){
    trace2(buf, p, size, lu);
    if(size == PAGE_SIZE){
        arena_free((arena_t *) buf);
        return;
    }
    rassert(max(size + sizeof(used_block_t), MIN_BLOCK), <=,
            ((used_block_t *) buf - 1)->size);
    _nfree(buf);
}

void block_init(block_t *b, size_t size, size_t l_size){
    b->size = size;
    b->free = 1;
    b->l_size = l_size;
    b->lanc = (lanchor_t) INITIALIZED_LANCHOR;
    assert(write_block_magics(b));
}

void free_arena_init(arena_t *a){
    trace4(a, p);
    *a = (arena_t) INITIALIZED_FREE_ARENA;
    assert(write_arena_magics(a));
}

arena_t *arena_new(void){
    arena_t *fa = stack_pop_lookup(arena_t, sanc, &free_arenas);
    if(!fa){
        fa = mmap(NULL, ARENA_SIZE * ARENA_ALLOC_BATCH, PROT_READ | PROT_WRITE,
                  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if(!fa)
            return NULL;
        /* Note that we start from 1. */
        for(int i = 1; i < ARENA_ALLOC_BATCH; i++){
            arena_t *extra = (void*) ((uptr_t) fa + i * ARENA_SIZE);
            free_arena_init(extra);
            stack_push(&extra->sanc, &free_arenas);
        }
        /* TODO This will result in wasteful magics writes, but that's
           dbg-only. */
        free_arena_init(fa);
    }
    
    return fa;
}

void arena_free(arena_t *freed){
    freed->sanc = (sanchor_t) INITIALIZED_SANCHOR;
    stack_push(&freed->sanc, &free_arenas);
}

used_block_t *alloc(size_t size, size_t alignment){
    trace2(size, lu, alignment, lu);
    assert(alignment_valid(alignment));
    assert(size <= MAX_BLOCK);

    used_block_t *found;
    
    size = umax(size, MIN_BLOCK);

    for(blist_t *b = blist_larger_than(size); b < &blists[NBLISTS]; b++)
        if( (found = alloc_from_blist(size, alignment, b)) )
            goto done;

    absorb_all_wayward_blocks(&private_arenas);
    arena_t *a = arena_new();
    if(!a)
        return NULL;

    block_init(a->heap, MAX_BLOCK, MAX_BLOCK);
    /* This will insert the shavings from found into the right blist. */
    found = alloc_from_block(a->heap, size, alignment);
    
done:
    assert(found);
    assert(found->size >= size);
    assert(aligned(found->data, alignment));
    return found;
}

used_block_t *alloc_from_blist(size_t enough, size_t alignment, blist_t *bl){
    block_t *found;
    FOR_EACH_LLOOKUP(found, block_t, lanc, &bl->blocks)
    {
        assert(free_block_valid(found));
        if(supports_alignment(found, enough, alignment)){
            list_remove(&found->lanc, &bl->blocks);
            return alloc_from_block(found, enough, alignment);
        }
        /* Should only fail due to alignment issues. */
        assert(alignment != MIN_ALIGNMENT);
    }
    return NULL;
}

int supports_alignment(block_t *b, size_t enough, size_t alignment){
    used_block_t *ub = (used_block_t *) b;
    return
        ub->size - ptrdiff(align_up(&ub->data, alignment), &ub->data)
        >= enough;
}

used_block_t *alloc_from_block(block_t *block, size_t size, size_t alignment){
    used_block_t *shaved = shave(block, size, alignment);
    shaved->free = 0;
    return shaved;
}

used_block_t *shave(block_t *b, size_t enough, size_t alignment){
    trace3(b,p,enough,lu,alignment,lu);

    /* Move max shaved space to front while preserving alignment. */
    used_block_t *newb =
        container_of(align_up(&((used_block_t *)b)->data, alignment),
                     used_block_t, data);
    newb = (used_block_t *)
        ((uptr_t) newb + ualign_down(b->size - enough - ptrdiff(newb, b),
                                     alignment));

    if((uptr_t) newb != (uptr_t) b){
        newb->size = b->size - ptrdiff(newb, b);

        r_neighbor((block_t *) newb)->l_size = newb->size;
        newb->l_size = b->size -= newb->size;

        log("Split - %p:%u | %p:%u", b, b->size, newb, newb->size);

        /* So chunks of mem at the front that are not large enough to contain
           a block_t are essentially leaked until their neighbors coalesce
           with them. */
        if(ptrdiff(newb, b) >= MIN_BLOCK)
            list_add_front(&b->lanc,
                           &blist_smaller_than(b->size)->blocks);
    }
    
    assert(aligned(&newb->data, alignment));
    return newb;
}

void dealloc(used_block_t *_b){
    trace2(_b, p);
    assert(used_block_valid(_b));
    block_t *b = (block_t *) _b;
    arena_t *arena = arena_of(b);
    if(arena->owner == pthread_self()){
        b = merge_adjacent(b);
        block_init(b, b->size, b->l_size);

        if(b->size == MAX_BLOCK && private_arenas.size > IDEAL_FULL_ARENAS)
            arena_free(arena);
        else
            list_add_front(&b->lanc, &blist_smaller_than(b->size)->blocks);
    }
    else
        return_wayward_block((wayward_block_t*) b, arena);
}

void return_wayward_block(wayward_block_t *b, arena_t *arena){
    stack_push(&b->sanc, &arena->wayward_blocks);
}

void absorb_all_wayward_blocks(list_t *arenas){
    arena_t *cur_arena;
    FOR_EACH_LLOOKUP(cur_arena, arena_t, lanc, arenas)
        absorb_arena_blocks(cur_arena);
}

void absorb_arena_blocks(arena_t *arena){
    wayward_block_t *block;    
    FOR_EACH_SPOPALL_LOOKUP(block, wayward_block_t, sanc,
                            &arena->wayward_blocks)
        dealloc((used_block_t *) block);
}


void *merge_adjacent(block_t *b){
    trace3(b, p);
    int loops = 0;
    block_t *near;    
    while( ((near = r_neighbor(b)) && near->free) ||
           ((near = l_neighbor(b)) && near->free) ) 
    {
        if(near->size >= MIN_BLOCK)
            list_remove(&near->lanc, &blist_smaller_than(near->size)->blocks);

        if(near < b)
            log("Merge - %p:%u | %p:%u", near, near->size, b, b->size);
        else
            log("Merge - %p:%u | %p:%u", b, b->size, near, near->size);

        block_t *oldb = b;
        b = (block_t *) umin(b, near);
        b->size = oldb->size + near->size;

        /* At most 1 merge each for l and r with the current eager coalescing
           policy. */
        assert(loops++ < 2);
        assert(near < oldb || loops == 1);
    }

    r_neighbor(b)->l_size = b->size;
    return b;
}

blist_t *blist_larger_than(size_t size){
    trace4(size, lu);
    assert(size <= MAX_BLOCK);
    for(int i = 0; i < NBLISTS; i++)
        if(blist_size_lookup[i] >= size)
            return &blists[i];
    return &blists[NBLISTS];
}

blist_t *blist_smaller_than(size_t size){
    trace4(size, lu);
    assert(size >= MIN_BLOCK);
    for(int i = NBLISTS - 1; i >= 0; i--)
        if(blist_size_lookup[i] <= size)
            return &blists[i];
    LOGIC_ERROR();
    return NULL;
}

block_t *l_neighbor(block_t *b){
    if((uptr_t) b == (uptr_t) arena_of(b)->heap)
        return &dummy;
    return (block_t *) ((uptr_t) b - b->l_size);
}

block_t *r_neighbor(block_t *b){
    arena_t *a = arena_of(b);
    if((uptr_t) b + b->size == (uptr_t) a + ARENA_SIZE)
        return &dummy;
    return (block_t *) ((uptr_t) b + b->size);
}

arena_t *arena_of(block_t *b){
    return (arena_t *) align_down(b, PAGE_SIZE);
}

int free_block_valid(block_t *b){
    assert(block_magics_valid(b));
    assert(b->size >= MIN_BLOCK);
    assert(b->size <= MAX_BLOCK);
    rassert(l_neighbor(b)->size, ==, b->l_size);
    assert(r_neighbor(b)->l_size == b->size || r_neighbor(b) == &dummy);
    assert(lanchor_valid(&b->lanc,
                         &blist_smaller_than(b->size)->blocks));
    if(l_neighbor(b) == &dummy)
        assert(b->l_size == MAX_BLOCK);
    assert(dummy.size == MAX_BLOCK);
    
    return 1;
}

int used_block_valid(used_block_t *b){
    assert(b->size >= MIN_BLOCK);
    assert(b->size <= MAX_BLOCK);
    assert(aligned(b->data, MIN_ALIGNMENT));
    rassert(l_neighbor((block_t*) b)->size, ==, b->l_size);
    assert(r_neighbor((block_t*) b)->l_size == b->size ||
           r_neighbor((block_t*) b) == &dummy);
    assert(dummy.size == MAX_BLOCK);

    return 1;
}

int write_block_magics(block_t *b){
    b->magics[0] = NALLOC_MAGIC_INT;
    if(!HEAP_DBG)
        return 1;
    for(int *m = &b->magics[0]; m < (int*)((uptr_t)b + b->size); m++)
        *m = NALLOC_MAGIC_INT;
    /* Just so that this can be wrapped in assert(). */
    return 1;
}

int block_magics_valid(block_t *b){
    assert((b->magics[0] = NALLOC_MAGIC_INT, 1));
    if(!HEAP_DBG)
        return 1;
    for(int *m = &b->magics[0]; m < (int*)((uptr_t)b + b->size); m++)
        assert(*m = NALLOC_MAGIC_INT);
    return 1;
}

int free_arena_valid(arena_t *a){
    /* assert(sanchor_valid(&a->sanc)); */
    assert(arena_magics_valid(a));
    return 1;
}

int used_arena_valid(arena_t *a){
    return 1;
}

int write_arena_magics(arena_t *a){
    int *magics = (int *) a->heap;
    magics[0] = 1;
    if(!ARENA_DBG)
        return 1;
    assert(aligned(ARENA_SIZE - offsetof(arena_t, heap), sizeof(int)));
    for(int i = 1; i < ARENA_SIZE/sizeof(int); i++)
        magics[i] = ARENA_MAGIC_INT;
    return 1;
}

int arena_magics_valid(arena_t *a){
    int *magics = (int *) a->heap;
    assert(magics[0] == ARENA_MAGIC_INT);
    if(!ARENA_DBG)
        return 1;
    for(int i = 1; i < ARENA_SIZE/sizeof(int); i++)
        assert(magics[i] == ARENA_MAGIC_INT);
    return 1;
}

int alignment_valid(int alignment){
    /* TODO */
    assert(alignment == MIN_ALIGNMENT || alignment == PAGE_SIZE);
    return 1;
}
