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
 */

#define MODULE NALLOC

#include <nalloc.h>
#include <list.h>
#include <stack.h>
#include <sys/mman.h>
#include <global.h>

#define MAX_ARENA_FAILS 10

static lfstack_t global_arenas = INITIALIZED_STACK;
__thread nalloc_info_t local = INITIALIZED_NALLOC_INFO;

#define dummy (local.dummy)

void *_nmalloc(size_t size){
    trace2(size, lu);
    used_block_t *found = alloc(size + sizeof(used_block_t), MIN_ALIGNMENT);
    return found ? found + 1 : NULL;
}

void *_nmemalign(size_t alignment, size_t size){
    trace2(alignment, lu, size, lu);
    used_block_t *found = alloc(size + sizeof(used_block_t), alignment);
    return found ? found + 1 : NULL;
}

void *_ncalloc(size_t nelt, size_t eltsize){
    trace2(nelt, lu, eltsize, lu);
    uint8_t *new = _nmalloc(nelt * eltsize);
    if(new)
        for(int i = 0; i < nelt * eltsize; i++)
            new[i] = 0;
    return new;
}

void *_nrealloc(void *buf, size_t new_size){
    trace2(buf, p, new_size, lu);
    void *new = _nmalloc(new_size);
    if(new && buf){
        memcpy(new, buf, ((used_block_t *) buf - 1)->size);
        _nfree(buf);
    }
    return new;
}
        
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

void *_nsmemalign(size_t alignment, size_t size){
    trace2(alignment, lu, size, lu);
    if(size == PAGE_SIZE && aligned(alignment, PAGE_SIZE))
        return arena_new();
    return _nmemalign(size, alignment);
}

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

free_arena_t *next_free_arena(free_arena_t *a){
    return (void*) ((uptr_t) a + a->size);
}

void free_arena_init(free_arena_t *a, size_t size){
    trace4(a, p, size, lu);
    *a = (free_arena_t ) {.sanc = INITIALIZED_SANCHOR,
                          .size = size,
                          .owner = 0};    
    assert(write_arena_magics(a));
}

arena_t *arena_new(void){
    free_arena_t *fa = stack_pop_lookup(free_arena_t, sanc, &global_arenas);
    if(!fa &&
       !(fa = mmap(NULL, PAGE_SIZE, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0)))
        return NULL;
    arena_init((arena_t *) fa);
    return (arena_t *) fa;
}

void arena_init(arena_t *arena){
    /* TODO */
    assert(aligned(arena, PAGE_SIZE));
    arena->free_space = MAX_BLOCK;
    arena->owner = pthread_self();
    arena->lanc = (lanchor_t) INITIALIZED_LANCHOR;
    arena->wayward_blocks = (lfstack_t) INITIALIZED_STACK;
    for(int l = 0; l < NBLISTS; l++)
        arena->blists[l] = (blist_t) INITIALIZED_BLIST();

    block_t *b = (block_t *) arena->heap;
    block_init(b, MAX_BLOCK, MAX_BLOCK);
    list_add_front(&b->lanc, &blist_smaller_than(b->size, arena)->blocks);
}    

void arena_free(arena_t *_freed){
    free_arena_t *freed = (free_arena_t *) _freed;
    free_arena_init(freed, PAGE_SIZE);
    stack_push(&freed->sanc, &global_arenas);
}


void block_init(block_t *b, size_t size, size_t l_size){
    b->size = size;
    b->free = 1;
    b->l_size = l_size;
    b->lanc = (lanchor_t) INITIALIZED_LANCHOR;
    assert(write_block_magics(b));
}

used_block_t *alloc(size_t size, size_t alignment){
    trace2(size, lu, alignment, lu);
    assert(alignment_valid(alignment));

    used_block_t *found;
    
    size = umax(size, MIN_BLOCK);
    assert(size <= MAX_BLOCK - sizeof(arena_t));

    int arena_fails = 0;
    arena_t *arena;
    FOR_EACH_LLOOKUP(arena, arena_t, lanc, &local.partial_arenas){
        /* TODO: A circular LL would be good.
           TODO: Or could swap while iterating. */
        found = alloc_from_arena(size, alignment, arena);
        if(found){
            if(arena->free_space == 0){
                list_remove(&arena->lanc, &local.partial_arenas);
                list_add_front(&arena->lanc, &local.empty_arenas);
            }
            goto done;
        }
        if(++arena_fails > MAX_ARENA_FAILS)
            break;
        PINT(arena_fails);
        PLUNT(arena->free_space);
        PLUNT(size);
    }

    /* absorb_all_wayward_blocks(); */

    arena = list_pop_lookup(arena_t, lanc, &local.full_arenas);
    if(!arena && !(arena = arena_new()))
        return NULL;
    found = alloc_from_arena(size, alignment, arena);

    if(found->size != MAX_BLOCK)
        list_add_front(&arena->lanc, &local.partial_arenas);
    else
        list_add_front(&arena->lanc, &local.empty_arenas);
    
done:
    assert(found);
    assert(found->size >= size);
    assert(aligned(found->data, alignment));
    return found;
}

used_block_t *alloc_from_arena(size_t size, size_t alignment, arena_t *arena){
    trace3(size, lu, alignment, lu);

    if(arena->free_space < size)
        return NULL;
    
    for(blist_t *bl = blist_larger_than(size, arena);        
        bl < &arena->blists[NBLISTS];
        bl++)
    {
        used_block_t *found = alloc_from_blist(size, alignment, arena, bl);
        if(found){
            arena->free_space -= found->size;
            return found;
        }
    }

    return NULL;

    used_block_t *found = alloc_from_blist(size, alignment, arena,
                                           blist_smaller_than(size, arena));
    return found;
}

used_block_t *alloc_from_blist(size_t enough, size_t alignment,
                               arena_t *arena, blist_t *bl){
    block_t *found;
    FOR_EACH_LLOOKUP(found, block_t, lanc, &bl->blocks)
    {
        assert(free_block_valid(found));
        if(supports_alignment(found, enough, alignment)){
            list_remove(&found->lanc, &bl->blocks);
            used_block_t *shaved = shave(found, enough, alignment, arena);
            shaved->free = 0;

            assert(used_block_valid(shaved));
            if(!(shaved->size >= enough &&
                 aligned(shaved->data, alignment)))
                BREAK;

            assert(shaved->size >= enough &&
                   aligned(shaved->data, alignment));
            return shaved;
        }
    }
    return NULL;
}

int supports_alignment(block_t *b, size_t enough, size_t alignment){
    used_block_t *ub = (used_block_t *) b;
    return
        ub->size - ptrdiff(align_up(&ub->data, alignment), &ub->data)
        >= enough;
}

used_block_t *shave(block_t *b, size_t enough,
                    size_t alignment, arena_t *arena)
{
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
                           &blist_smaller_than(b->size, arena)->blocks);
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
        
        b = merge_adjacent(b, arena);
        block_init(b, b->size, b->l_size);
        list_add_front(&b->lanc, &blist_smaller_than(b->size, arena)->blocks);
        
        arena->free_space += b->size;
        if(arena->free_space == b->size)
            list_remove(&arena->lanc, &local.empty_arenas);
        else if(arena->free_space == MAX_BLOCK)
            list_remove(&arena->lanc, &local.partial_arenas);

        if(arena->free_space == MAX_BLOCK){
            if(local.full_arenas.size > IDEAL_FULL_ARENAS)
                arena_free(arena);
            else
                list_add_front(&arena->lanc, &local.full_arenas);
        }
    }
    else
        return_wayward_block((wayward_block_t*) b, arena);
}

void return_wayward_block(wayward_block_t *b, arena_t *arena){
    /* And what if the owner is dead? */
    stack_push(&b->sanc, &arena->wayward_blocks);
}

void absorb_all_wayward_blocks(void){
    arena_t *cur_arena;
    FOR_EACH_LLOOKUP(cur_arena, arena_t, lanc, &local.partial_arenas)
        absorb_arena_blocks(cur_arena);
    FOR_EACH_LLOOKUP(cur_arena, arena_t, lanc, &local.empty_arenas)
        absorb_arena_blocks(cur_arena);
}

void absorb_arena_blocks(arena_t *arena){
    wayward_block_t *block;    
    FOR_EACH_SPOPALL_LOOKUP(block, wayward_block_t, sanc,
                            &arena->wayward_blocks)
        dealloc((used_block_t *) block);
}


void *merge_adjacent(block_t *b, arena_t *arena){
    trace3(b, p);
    int loops = 0;
    block_t *near;    
    while( ((near = r_neighbor(b)) && near->free) ||
           ((near = l_neighbor(b)) && near->free) ) 
    {
        if(near->size >= MIN_BLOCK)
            list_remove(&near->lanc,
                        &blist_smaller_than(near->size, arena)->blocks);

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

blist_t *blist_larger_than(size_t size, arena_t *arena){
    trace4(size, lu, arena, p);
    assert(size <= MAX_BLOCK);
    for(int i = 0; i < NBLISTS; i++)
        if(blist_size_lookup[i] >= size)
            return &arena->blists[i];
    return &arena->blists[NBLISTS];
}

blist_t *blist_smaller_than(size_t size, arena_t *arena){
    trace4(size, lu, arena, p);
    assert(size >= MIN_BLOCK);
    for(int i = NBLISTS - 1; i >= 0; i--)
        if(blist_size_lookup[i] <= size)
            return &arena->blists[i];
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
                         &blist_smaller_than(b->size, arena_of(b))->blocks));
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

int free_arena_valid(free_arena_t *a){
    assert(a->size == PAGE_SIZE);
    /* assert(sanchor_valid(&a->sanc)); */
    assert(arena_magics_valid(a));
    return 1;
}

int used_arena_valid(arena_t *a){
    return 1;
}

int write_arena_magics(free_arena_t *a){
    a->magics[0] = 1;
    if(!ARENA_DBG)
        return 1;
    assert(aligned(a->size - offsetof(free_arena_t, magics), sizeof(int)));
    for(int *m = &a->magics[0]; m < (int *)((uptr_t) a + a->size); m++)
        *m = ARENA_MAGIC_INT;
    return 1;
}

int arena_magics_valid(free_arena_t *a){
    assert(a->magics[0] == ARENA_MAGIC_INT);
    if(!ARENA_DBG)
        return 1;
    for(int *m = &a->magics[0]; m < (int *)((uptr_t) a + a->size); m++)
        assert(*m == ARENA_MAGIC_INT);
    return 1;
}

int alignment_valid(int alignment){
    /* TODO */
    assert(alignment == MIN_ALIGNMENT || alignment == PAGE_SIZE);
    return 1;
}
