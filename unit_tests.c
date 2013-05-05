/**
 * @file   unit_tests.c
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * 
 * @brief  Tests to be run by the kernel itself, right before loading init.
 *
 * A lot of knick-knacks.
 */

#define MODULE UNIT_TESTS
#define _GNU_SOURCE

#include <list.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <nalloc.h>
#include <stdlib.h>
#include <global.h>

typedef void *(entrypoint_t)(void *);

#define _yield(tid) pthread_yield()
#define _wait wait
#define exit(val) pthread_exit((void *) val)
#define kfork(entry, arg, flag)                 \
    pthread_create(&kids[i], NULL, entry, arg)  \

#define wsmalloc _nsmalloc
#define wsfree _nsfree

/* #define wsmalloc malloc */
/* #define wsfree(ptr, size) free(ptr) */

#define report_profile() 

/* #define NUM_MALLOC_TESTERS 1000 */
#define NUM_MALLOC_TESTERS 1
#define NUM_ALLOCATIONS 1000
#define NUM_OPS 50 * NUM_ALLOCATIONS
#define MAX_WRITES  8
#define REPORT_INTERVAL 100
#define MAX_SIZE 128
#define SIZE1 12
#define SIZE2 16
static int rdy;

static __thread unsigned int seed;
void prand_init(void){
    assert(read(open("/dev/urandom", O_RDONLY), &seed, sizeof(seed)) ==
           sizeof(seed));
    /* assert(initstate(seed, prand_state, ARR_LEN(prand_state)) != prand_state); */
}
long int prand(void){
    return rand_r(&seed);
    /* return random(); */
}
int rand_percent(int per_centum){
    return prand() % 100 <= per_centum;
}

void mt_child(int parent_tid);

void malloc_test(void){
    trace();
    int status = 0;
    int ret = 0;
    int i;

    (void) ret;
    (void) status;

    pthread_t kids[NUM_MALLOC_TESTERS];

    for(i = 0; i < NUM_MALLOC_TESTERS; i++){
        if(kfork((entrypoint_t *) mt_child, (void *) _gettid(), KERN_ONLY) < 0)
            LOGIC_ERROR("Failed to fork.");
    }

    rdy = TRUE;

    for(i = 0; i < NUM_MALLOC_TESTERS; i++)
        assert(!pthread_join(kids[i], (void *[]){NULL}));
}

/* #define test_free(x) pfree(x, size, size == SIZE1 ? &pool1 : &pool2) */
/* #define test_malloc(x) pwsmalloc(x, size == SIZE1 ? &pool1 : &pool2) */
#define test_free(x) wsfree(x, size)
#define test_malloc(x) wsmalloc(x)

/* Note: can be used to test speed of malloc too. How many times do you manage
   to go into the allocator before being preempted? */
void mt_child(int parent_tid){
    int *blocks[NUM_ALLOCATIONS];
    int size = (_gettid() % 2) ? SIZE1 : SIZE2;
    volatile int allocated = 0;
    int malloc_prob;
    volatile int i, j;
    int jid = _gettid();

    UNIMPLEMENTED("Get rid of the stack");

    while(rdy == FALSE)
        _yield(parent_tid);
    
    for(i = 0; i < NUM_OPS; i++){
        if(allocated > 0 && allocated % REPORT_INTERVAL == 0)
            log2("i:%d allocated:%d", i, allocated);
        
        if(allocated == NUM_ALLOCATIONS)
            test_free(blocks[--allocated]);

        if(allocated >= NUM_ALLOCATIONS/2)
            malloc_prob = 50;
        else
            malloc_prob = 75;
        
        if(rand_percent(malloc_prob)){
            if( (blocks[allocated++] = test_malloc(size)) == NULL)
                LOGIC_ERROR("We shouldn't have run out of space.");
            for(j = 0; j < size/sizeof(int); j++)
                blocks[allocated - 1][j] = jid;
        }
        else if(allocated > 0){
            for(j = 0; j < size/sizeof(int); j++)
                assert(blocks[allocated - 1][j] == jid);
            test_free(blocks[--allocated]);
        }
    }

    /* Clean up. */
    for(i = 0; i < allocated; i++){
        test_free(blocks[i]);
    }

    report_profile();
    /* exit(_gettid()); */
}

void mt_child2(int parent_tid);

void malloc_test_randsize(){
    trace();
    int status = 0;
    int ret = 0;
    int i;

    (void) ret;
    (void) status;

    pthread_t kids[NUM_MALLOC_TESTERS];
    for(i = 0; i < NUM_MALLOC_TESTERS; i++){
        if(kfork((entrypoint_t *) mt_child2, (void *) _gettid(), KERN_ONLY) < 0)
            LOGIC_ERROR("Failed to fork.");
    }

    rdy = TRUE;

    for(i = 0; i < NUM_MALLOC_TESTERS; i++)
        assert(!pthread_join(kids[i], (void *[]){NULL}));
}

void mt_child2(int parent_tid){
    trace2(parent_tid, d);
    
    list_t blocks = INITIALIZED_LIST;
    int jid = _gettid();
    prand_init();
    
    struct block_t{
        lanchor_t lanc;
        int size;
        int magics[];
    } *cur_block;

    while(rdy == FALSE)
        _yield(parent_tid);
    
    for(int i = 0; i < NUM_OPS; i++){
        int size;
        int malloc_prob =
            blocks.size < NUM_ALLOCATIONS/2 ? 75 :
            blocks.size < NUM_ALLOCATIONS ? 50 :
            blocks.size < NUM_ALLOCATIONS * 2 ? 25 :
            0;
        
        if(blocks.size > 0 && blocks.size % REPORT_INTERVAL == 0)
            log2("i:%d allocated:%d", i, blocks.size);

        if(rand_percent(malloc_prob)){
            size = prand() % (MAX_SIZE);
            cur_block = wsmalloc(size);
            if(!cur_block)
                continue;
            *cur_block = (struct block_t)
                { . size = size, .lanc = INITIALIZED_LANCHOR };
            /* Why doesn't this cause corruption if size isn't int aligned? */
            for(int *m = cur_block->magics;
                (uptr_t) m < (uptr_t) cur_block +
                    umin(size, MAX_WRITES);
                m++)
                *m = jid;
            list_add_rear(&cur_block->lanc, &blocks);
        }else if(blocks.size){
            cur_block =
                lookup_lanchor(list_nth(prand() % blocks.size,
                                        &blocks),
                               struct block_t, lanc);
            if(!cur_block)
                continue;
            list_remove(&cur_block->lanc, &blocks);
            for(int *m = cur_block->magics;
                (uptr_t) m < (uptr_t) cur_block +
                    umin(cur_block->size, MAX_WRITES);
                m++)
                assert(*m == jid);
            wsfree(cur_block, cur_block->size);
        }
    }

    FOR_EACH_LLOOKUP(cur_block, struct block_t, lanc, &blocks){
        /* Hacky way to make the iteration safe after freeing. */
        struct block_t local_copy = *cur_block;
        wsfree(cur_block, cur_block->size);
        cur_block = &local_copy;
    }

    report_profile();
    /* exit(_gettid()); */
}

void mt_sharing_child();

struct block_t{
    sanchor_t sanc;
    int size;
    int magics[];
};

#define NUM_STACKS 8
void malloc_test_sharing(){
    trace();
    int status = 0;
    int ret = 0;
    int i;

    struct{
        int parent_tid;
        lfstack_t block_stacks[NUM_STACKS];
    } shared;
    shared.parent_tid = _gettid();
    for(int i = 0; i < NUM_STACKS; i++)
        shared.block_stacks[i] = (lfstack_t) INITIALIZED_STACK;

    pthread_t kids[NUM_MALLOC_TESTERS];
    for(i = 0; i < NUM_MALLOC_TESTERS; i++){
        if(kfork((entrypoint_t *) mt_sharing_child, &shared, KERN_ONLY) < 0)
            LOGIC_ERROR("Failed to fork.");
    }

    rdy = TRUE;

    for(i = 0; i < NUM_MALLOC_TESTERS; i++)
        assert(!pthread_join(kids[i], (void *[]){NULL}));

    for(int i = 0; i < NUM_STACKS; i++){
        lfstack_t *blocks = &shared.block_stacks[i];
        struct block_t *cur_block;
        FOR_EACH_SPOP_LOOKUP(cur_block, struct block_t, sanc, blocks)
            wsfree(cur_block, cur_block->size);
    }
}

void mt_sharing_child(
    struct{int parent_tid; lfstack_t block_stacks[NUM_STACKS];} *shared)
{
    int parent_tid = shared->parent_tid;
    int jid = _gettid();
    struct block_t *cur_block;
    prand_init();

    while(rdy == FALSE)
        _yield(parent_tid);

    lfstack_t priv_blocks = INITIALIZED_STACK;
    for(int i = 0; i < NUM_OPS; i++){
        int size;
        lfstack_t *blocks= &shared->block_stacks[prand() % NUM_STACKS];
        int malloc_prob =
            blocks->size < NUM_ALLOCATIONS/2 ? 75 :
            blocks->size < NUM_ALLOCATIONS ? 50 :
            blocks->size < NUM_ALLOCATIONS * 2 ? 25 :
            0;
        
        if(blocks->size > 0 && blocks->size % REPORT_INTERVAL == 0)
            log2("i:%d allocated:%d", i, blocks->size);

        if(rand_percent(malloc_prob)){
            size = prand() % (MAX_SIZE);
            cur_block = wsmalloc(size);
            if(!cur_block)
                continue;
            log2("Allocated: %p", cur_block);
            *cur_block = (struct block_t)
                { .size = size, .sanc = INITIALIZED_SANCHOR };
            /* Try to trigger false sharing. */
            for(volatile int *m = cur_block->magics;
                (uptr_t) m < (uptr_t) cur_block + umin(size, MAX_WRITES); m++)
                *m = jid;
            stack_push(&cur_block->sanc, blocks);
        }else if(blocks->size){
            cur_block =
                stack_pop_lookup(struct block_t, sanc, blocks);
            if(cur_block){
                log2("Claiming: %p", cur_block);
                PINT(cur_block->size);
                for(int *m = cur_block->magics;
                    (uptr_t) m < (uptr_t) cur_block + umin(cur_block->size,
                                                           MAX_WRITES);
                    m++)
                {
                    *m = jid;
                }
                stack_push(&cur_block->sanc, &priv_blocks);
            }
        }

        if(rand_percent(50)){
            cur_block =
                stack_pop_lookup(struct block_t, sanc, &priv_blocks);
            if(!cur_block)
                continue;
            log2("Freeing priv: %p", cur_block);
            for(int *m = cur_block->magics;
                (uptr_t) m < (uptr_t) cur_block + umin(cur_block->size,
                                                       MAX_WRITES);
                m++)
            {
                rassert(*m, ==, jid);
            }
            wsfree(cur_block, cur_block->size);
        }
    }

    report_profile();
    /* exit(_gettid()); */
}

void *test(void *);

#include <time.h>
int main(){
    struct timespec start;
    assert(!clock_gettime(CLOCK_MONOTONIC, &start));
    
    /* malloc_test(); */
    /* malloc_test_randsize(); */
    TIME(malloc_test_sharing());

    void *tst;
    TIME(tst = malloc(20));
    TIME(free(tst));
    
    struct timespec end;
    
    return 0;
}

