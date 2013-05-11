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

#ifdef HIDE_NALLOC
#define malloc nmalloc
#define free nfree
#define calloc ncalloc
#define realloc nrealloc
#endif

#define NALLOC_MAGIC_INT 0x999A110C

#define PAGE_SHIFT 12
#define PAGE_SIZE (1 << PAGE_SHIFT)
#define ARENA_SIZE (PAGE_SIZE >> 1)

#define MIN_ALIGNMENT 16

#define SLAB_ALLOC_BATCH 16
#define IDEAL_FULL_SLABS 5

#define MAX_BLOCK (const_align_down_pow2(PAGE_SIZE - sizeof(arena_t), MIN_ALIGNMENT))

static const int blist_size_lookup[] = {
    8, 16, 24, 32, 48, 64, 80, 96, 112, 256, 512, 1024,
};
#define NBLISTS ARR_LEN(blist_size_lookup)

typedef struct{
    sanchor_t sanc;
} block_t __attribute__((__aligned__(4)));
COMPILE_ASSERT(sizeof(block_t) <= MIN_BLOCK);

typedef struct{
    lfstack_t blocks[NBLISTS];
} bcache_t;

typedef struct{
    size_t size;
    void *data[];
} large_block_t;

COMPILE_ASSERT(sizeof(large_block_t) != sizeof(arena_t));

typedef struct{
    lfstack_t *wayward_blocks;
    sanchor_t sanc;
    unsigned int size;
} arena_t;

typedef struct {
    size_t num_bytes;
    size_t num_arenas;
    size_t num_bytes_highwater;
    size_t num_arenas_highwater;
} nalloc_profile_t;

void *nmalloc(size_t size);
void nfree(void *buf);

void nalloc_init(void);
arena_t *arena_new(void);
void arena_free(arena_t *arena);               
void free_arena_init(arena_t *a);
void arena_init(arena_t *arena);

void *large_alloc(size_t size);
void large_dealloc(large_block_t *block);

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

void profile_bytes(size_t nbytes);
void profile_arenas(size_t narenas);

int free_arena_valid(arena_t *a);
int used_arena_valid(arena_t *a);

int free_block_valid(block_t *b);
int used_block_valid(used_block_t *b);
int write_block_magics(block_t *b);
int block_magics_valid(block_t *b);
   
#endif
