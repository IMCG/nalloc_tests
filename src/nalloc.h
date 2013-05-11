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

#define ARENA_ALLOC_BATCH 16
#define IDEAL_FULL_ARENAS 5

#define MIN_POW 5
#define MAX_POW 12

#define MAX_BLOCK (const_align_down_pow2(PAGE_SIZE - offsetof(arena_t, heap), \
                                         MIN_BLOCK))

#define MIN_BLOCK (1 << MIN_POW)
#define MIN_ALIGNMENT 16
COMPILE_ASSERT(aligned(MIN_BLOCK, MIN_ALIGNMENT));
COMPILE_ASSERT(aligned(MIN_ALIGNMENT, sizeof(long long)));

static const int blist_size_lookup[] = {
    32, 48, 64, 80, 96, 112, 256, 512, 1024,
};
/* Has to be a natural number for REPEATING_LIST(). */
#define NBLISTS 9
COMPILE_ASSERT(ARR_LEN(blist_size_lookup) == NBLISTS);

typedef struct{
    unsigned int free:1;
    unsigned int size:MAX_POW;
    unsigned int l_size:MAX_POW;
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

typedef struct{
    unsigned int free:1;
    unsigned int size:MAX_POW;
    unsigned int l_size:MAX_POW;
    int data[];
} used_block_t;

COMPILE_ASSERT(sizeof(used_block_t) == 4);

typedef struct{
    unsigned int free:1;
    unsigned int size:MAX_POW;
    unsigned int l_size:MAX_POW;
    sanchor_t sanc;
    void *data[];
} wayward_block_t;

#define INITIALIZED_BLIST {.blocks = INITIALIZED_LIST}

typedef struct  __attribute__ ((packed)){
    lfstack_t disowned_blocks;
    union{
        sanchor_t sanc;
        lanchor_t lanc;
    };
    lfstack_t *wayward_blocks;
    uint8_t pad[4];
    block_t heap[];
} arena_t;

COMPILE_ASSERT(MAX_BLOCK < 1 << MAX_POW);

/* This makes guaranteeing min alignment trivial. */
COMPILE_ASSERT(aligned(offsetof(arena_t, heap) +
                       offsetof(used_block_t, data),
                       MIN_ALIGNMENT));
COMPILE_ASSERT(
    const_align_up_pow2(MAX_BLOCK + sizeof(arena_t), MIN_ALIGNMENT) <= ARENA_SIZE);

#define INITIALIZED_FREE_ARENA(self_ptr)                                \
    {                                                                   \
        .disowned_blocks = INITIALIZED_STACK,                           \
            .sanc = INITIALIZED_SANCHOR,                                \
            .wayward_blocks = &(self_ptr)->disowned_blocks,             \
            }


void *nmalloc(size_t size);
void nfree(void *buf);

void nalloc_init(void);
arena_t *arena_new(void);
void arena_free(arena_t *arena);               
void free_arena_init(arena_t *a);
void arena_init(arena_t *arena);

used_block_t *alloc(size_t size);
used_block_t *alloc_from_blist(size_t enough, blist_t *bl);
used_block_t *alloc_from_block(block_t *block, size_t size);
used_block_t *shave(block_t *b, size_t enough);

void dealloc(used_block_t *_b);
void *merge_adjacent(block_t *b);

void return_wayward_block(wayward_block_t *b, arena_t *arena);
used_block_t *alloc_from_wayward_blocks(size_t size);

void block_init(block_t *b, size_t size, size_t l_size);
int supports_alignment(block_t *b, size_t enough, size_t alignment);

blist_t *blist_smaller_than(size_t size);
blist_t *blist_larger_than(size_t size);

block_t *r_neighbor(block_t *b);
block_t *l_neighbor(block_t *b);

void free_arenas_atexit();
int register_thread_destructor(void);

arena_t *arena_of(block_t *b);
int is_junk_block(block_t *b);

int free_arena_valid(arena_t *a);
int used_arena_valid(arena_t *a);

int free_block_valid(block_t *b);
int used_block_valid(used_block_t *b);
int write_block_magics(block_t *b);
int block_magics_valid(block_t *b);
   
#endif
