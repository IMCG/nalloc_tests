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
#include <peb_util.h>
#include <pthread.h>
#include <refcount.h>
#include <stack.h>

#ifdef HIDE_NALLOC
#define malloc nmalloc
#define free nfree
#define calloc ncalloc
#define realloc nrealloc
#endif

#define NALLOC_MAGIC_INT 0x999A110C

#define CACHELINE_SHIFT 6
#define CACHELINE_SIZE (1 << 6)

#define PAGE_SHIFT 12
#define PAGE_SIZE (1 << PAGE_SHIFT)
#define SLAB_SIZE (PAGE_SIZE >> 1)

#define MIN_ALIGNMENT 16

#define SLAB_ALLOC_BATCH 16
#define IDEAL_FULL_SLABS 5

#define MIN_BLOCK 16
#define MAX_BLOCK (SLAB_SIZE / 8)

static const int bcache_size_lookup[] = {
    16, 24, 32, 48, 64, 80, 96, 112, 256, 512, 
};
#define NBSTACKS ARR_LEN(bcache_size_lookup)

typedef struct{
    size_t size;
} large_block_t;

typedef struct{
    __attribute__((__aligned__(MIN_ALIGNMENT)))
    sanchor_t sanc;
} block_t;
COMPILE_ASSERT(sizeof(block_t) <= MIN_BLOCK);

typedef struct{
    lfstack_t blocks[NBSTACKS];
    refcount_t refs;
} wayward_bcache_t __attribute__((__aligned__(CACHELINE_SIZE)));

typedef struct{
    wayward_bcache_t *wayward_blocks;
    simpstack_t hot_blocks;
    sanchor_t sanc;
    unsigned int nblocks_contig;
    size_t block_size;
    block_t blocks[];
} slab_t;

#define FRESH_SLAB {                            \
        .wayward_blocks = NULL,                 \
            .hot_blocks = FRESH_STACK,          \
            .sanc = FRESH_SANCHOR,              \
            .nblocks_contig = 0,                \
            .block_size = 0,                    \
            }

COMPILE_ASSERT(sizeof(large_block_t) != sizeof(slab_t));
COMPILE_ASSERT(aligned_pow2(sizeof(slab_t), MIN_ALIGNMENT));


typedef struct{
    uint32_t nblocks;
    wayward_bcache_t bcaches[];
} wayward_bcache_slab_t;

#define FRESH_WAYWARD_BCACHE_SLAB {.nblocks = 0}

COMPILE_ASSERT(sizeof(large_block_t) != sizeof(slab_t));

typedef struct {
    size_t num_bytes;
    size_t num_slabs;
    size_t num_bytes_highwater;
    size_t num_slabs_highwater;
} nalloc_profile_t;

void *malloc(size_t size);
void free(void *buf);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);

static void *large_alloc(size_t size);
static void large_dealloc(large_block_t *block);

static int round_and_get_bcacheidx(size_t *size);

static void *alloc(size_t size);

static int slab_is_hot(slab_t *slab);
static int slab_is_empty(slab_t *slab);
static void *alloc_from_slab(slab_t *slab);
static slab_t *slab_install_new(size_t size, int sizeidx);
static void slab_free(slab_t *freed);

static block_t *alloc_from_wayward_blocks(int cidx);

static void dealloc(block_t *b);
static void return_wayward_block(block_t *b, slab_t *slab, int cidx);

static slab_t *slab_of(block_t *b);

static void free_slabs_atexit();
static void jump_through_hoop(void);
static int dynamic_init(void);
static int wayward_bcache_install_new(void);
static void wayward_bcache_free(wayward_bcache_t *c);

static void profile_bytes(size_t nbytes);
static void profile_slabs(size_t nslabs);
nalloc_profile_t *get_profile(void);

static int write_block_magics(block_t *b);
static int block_magics_valid(block_t *b);

#endif
