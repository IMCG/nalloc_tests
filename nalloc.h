/**
 * @file   nalloc.h
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * @date   Sun Oct 28 16:19:37 2012
 * 
 * @brief  
 * 
 */

#ifndef NALLOC_H
#define NALLOC_H

#include <list.h>
#include <peb_macros.h>
#include <pthread.h>
#include <stack.h>

#define NALLOC_MAGIC_INT 0x999A110C
#define ARENA_MAGIC_INT 0xD15C0BA1

#define PAGE_SIZE (0x1000)
#define ARENA_SIZE (PAGE_SIZE)

#define MIN_POW 4
#define MAX_POW 12

#define NBLISTS (MAX_POW - MIN_POW + 1)

#define MAX_BLOCK (1 << MAX_POW)
#define MIN_BLOCK (1 << MIN_POW)
#define MIN_ALIGNMENT 8
COMPILE_ASSERT(aligned(MIN_ALIGNMENT, sizeof(void*)));

typedef struct{
    unsigned int free:1;
    unsigned int size:MAX_POW + 1;
    unsigned int l_size:MAX_POW + 1;
    /* Padding. */
    lanchor_t lanc;
    int magics[];
} block_t;

COMPILE_ASSERT(sizeof(block_t) <= MIN_BLOCK);
COMPILE_ASSERT(sizeof(block_t) == 4 + sizeof((block_t){}.lanc));

/* TODO stack.h needs lifecycle.h */
typedef struct{
    list_t arenas;
    block_t dummy;
} nalloc_info_t;

#define INITIALIZED_NALLOC_INFO                                 \
    {                                                           \
        .arenas = INITIALIZED_LIST,                             \
            .dummy = { .free = 0,                               \
                       .size = MAX_BLOCK,                       \
                       .l_size = MAX_BLOCK - sizeof(arena_t)}   \
    }

#include <stack.h>

typedef struct{
    size_t size;
    list_t blocks;
} blist_t;

#define INITIALIZED_BLIST(_size) {.size = _size, .blocks = INITIALIZED_LIST}

typedef struct{
    size_t size;
    pthread_t owner;
    sanchor_t sanc;
    int magics[];
} free_arena_t;

typedef struct{
    size_t size;
    pthread_t owner;
    lanchor_t lanc;
    lfstack_t wayward_blocks;
    /* TODO: Make sure this doth not dangle. */
    blist_t blists[NBLISTS];
    void *heap[];
} arena_t;

typedef struct{
    unsigned int free:1;
    unsigned int size:MAX_POW + 1;
    unsigned int l_size:MAX_POW + 1;
    void *data[];
} used_block_t;

COMPILE_ASSERT(sizeof(used_block_t) == 4);

typedef struct{
    unsigned int free:1;
    unsigned int size:MAX_POW + 1;
    unsigned int l_size:MAX_POW + 1;
    sanchor_t sanc;
    void *data[];
} wayward_block_t;

void *_nmalloc(size_t size);
void *_nmemalign(size_t alignment, size_t size);
void *_ncalloc(size_t nelt, size_t eltsize);
void *_nrealloc(void *buf, size_t new_size);
void _nfree(void *buf);

void *_nsmalloc(size_t size);
void *_nsmemalign(size_t alignment, size_t size);
void _nsfree(void *buf, size_t size);

void nalloc_init(void);
arena_t *arena_new(void);
void arena_free(arena_t *arena);               
void free_arena_init(free_arena_t *a, size_t size);
void arena_init(arena_t *arena);
free_arena_t *next_free_arena(free_arena_t *a);

used_block_t *alloc(size_t size, size_t alignment);
used_block_t *alloc_from_arena(size_t size, size_t alignment, arena_t *arena);
used_block_t *alloc_from_blist(size_t enough, size_t alignment, arena_t *arena,
                          blist_t *bl);
used_block_t *shave(block_t *b, size_t enough,
                    size_t alignment, arena_t *arena);

void dealloc(used_block_t *_b);
void *merge_adjacent(block_t *b, arena_t *arena);

void return_wayward_block(wayward_block_t *b, arena_t *arena);
int absorb_wayward_block(arena_t *arena);
void absorb_all_wayward_blocks(void);

void block_init(block_t *b, size_t size, size_t l_size);
arena_t *arena_of(block_t *b);
int supports_alignment(block_t *b, size_t enough, size_t alignment);

blist_t *blist_smaller_than(size_t size, arena_t *arena);
blist_t *blist_larger_than(size_t size, arena_t *arena);

block_t *r_neighbor(block_t *b);
block_t *l_neighbor(block_t *b);

int free_arena_valid(free_arena_t *a);
int used_arena_valid(arena_t *a);
int write_arena_magics(free_arena_t *a);
int arena_magics_valid(free_arena_t *a);

int free_block_valid(block_t *b);
int used_block_valid(used_block_t *b);
int write_block_magics(block_t *b);
int block_magics_valid(block_t *b);

int alignment_valid(int alignment);
   
#endif
