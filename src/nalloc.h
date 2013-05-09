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

#define ARENA_ALLOC_BATCH 4
#define IDEAL_FULL_ARENAS 5

#define MIN_POW 5
#define MAX_POW 12

#define MAX_BLOCK (ARENA_SIZE - offsetof(arena_t, heap))
#define MIN_BLOCK (1 << MIN_POW)
#define MIN_ALIGNMENT 16
COMPILE_ASSERT(aligned(MIN_ALIGNMENT, sizeof(long long)));

static const int blist_size_lookup[] = {
    32, 48, 64, 80, 112, 256, 512, 1024,
};
/* Has to be a natural number for REPEATING_LIST(). */
#define NBLISTS 8
COMPILE_ASSERT(ARR_LEN(blist_size_lookup) == NBLISTS);

typedef struct{
    unsigned int free:1;
    unsigned int size:MAX_POW + 1;
    unsigned int l_size:MAX_POW + 1;
    lanchor_t lanc;

    /* __aligned__ didn't do the trick, surprisingly. GCC manual mentions
       linked limitations. */
    uint8_t manpad[8];

    int magics[];
} block_t __attribute__((__aligned__(32)));

COMPILE_ASSERT(sizeof(unsigned int) == 4);
COMPILE_ASSERT(sizeof(block_t) <= MIN_BLOCK);
COMPILE_ASSERT(sizeof(block_t) == 32);

typedef struct{
    list_t blocks;
} blist_t;

#define INITIALIZED_BLIST {.blocks = INITIALIZED_LIST}

/* TODO: Need a list of blocks here. */
typedef struct{
    pthread_t owner;
    lfstack_t wayward_blocks;
    union{
        sanchor_t sanc;
        lanchor_t lanc;
    };
    block_t heap[];
} arena_t;

#define INITIALIZED_FREE_ARENA                                          \
    {                                                                   \
        .owner = 0,                                                     \
            .wayward_blocks = INITIALIZED_STACK,                        \
            .sanc = INITIALIZED_SANCHOR,                                \
            }

/* Depends on arena_t def. */
COMPILE_ASSERT(MAX_BLOCK <= 1 << MAX_POW);

typedef struct{
    unsigned int free:1;
    unsigned int size:MAX_POW + 1;
    unsigned int l_size:MAX_POW + 1;
    int data[];
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
/* void *_nmemalign(size_t alignment, size_t size); */
/* void *_ncalloc(size_t nelt, size_t eltsize); */
/* void *_nrealloc(void *buf, size_t new_size); */
void _nfree(void *buf);

void *_nsmalloc(size_t size);
void *_nsmemalign(size_t alignment, size_t size);
void _nsfree(void *buf, size_t size);

void nalloc_init(void);
arena_t *arena_new(void);
void arena_free(arena_t *arena);               
void free_arena_init(arena_t *a);
void arena_init(arena_t *arena);

used_block_t *alloc(size_t size, size_t alignment);
used_block_t *alloc_from_blist(size_t enough, size_t alignment, blist_t *bl);
used_block_t *alloc_from_block(block_t *block, size_t size, size_t alignment);
used_block_t *shave(block_t *b, size_t enough, size_t alignment);

void dealloc(used_block_t *_b);
void *merge_adjacent(block_t *b);

void return_wayward_block(wayward_block_t *b, arena_t *arena);
void absorb_all_wayward_blocks(list_t *arenas);
void absorb_arena_blocks(arena_t *arena);

void block_init(block_t *b, size_t size, size_t l_size);
arena_t *arena_of(block_t *b);
int supports_alignment(block_t *b, size_t enough, size_t alignment);

blist_t *blist_smaller_than(size_t size);
blist_t *blist_larger_than(size_t size);

block_t *r_neighbor(block_t *b);
block_t *l_neighbor(block_t *b);

int free_arena_valid(arena_t *a);
int used_arena_valid(arena_t *a);
int write_arena_magics(arena_t *a);
int arena_magics_valid(arena_t *a);

int free_block_valid(block_t *b);
int used_block_valid(used_block_t *b);
int write_block_magics(block_t *b);
int block_magics_valid(block_t *b);

int alignment_valid(int alignment);
   
#endif
