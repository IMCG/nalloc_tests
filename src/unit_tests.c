/**
 * @file   unit_tests.c
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * 
 * @brief  Tests to be run by the kernel itself, right before loading init.
 *
 * A lot of knick-knacks.
 */

#define MODULE UNIT_TESTS

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
#define exit(val) pthread_exit((void *) val)
#define kfork(entry, arg, flag)                 \
    pthread_create(&kids[i], NULL, entry, arg)  \

#define wsmalloc(size) nmalloc(size)
#define wsfree(ptr, size) nfree(ptr)

/* #define wsmalloc malloc */
/* #define wsfree(ptr, size) free(ptr) */

#define report_profile() 

/* #define NUM_MALLOC_TESTERS 1000 */
#define NUM_MALLOC_TESTERS 10
#define NUM_ALLOCATIONS 1000
#define NUM_OPS 1000 * NUM_ALLOCATIONS
#define MAX_WRITES  0
#define REPORT_INTERVAL 100
#define MAX_SIZE 128
#define MIN_SIZE (sizeof(tblock_t) + 1) 

#define NUM_STACKS 32

/* Fun fact: without the volatile, a test-yield loop on rdy will optimize the
   test part and will just yield in a loop forever. Thanks, GCC! */
static volatile int rdy;

typedef struct{
    union{
        lanchor_t lanc;
        sanchor_t sanc;
    };
    size_t size;
    int magics[];
} tblock_t;

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

void write_magics(tblock_t *b, int tid){
    for(int i = 0;
        i < min((b->size - sizeof(*b)) / sizeof(b->magics[0]), MAX_WRITES);
        i++)
        b->magics[i] = tid;
}

void check_magics(tblock_t *b, int tid){
    for(int i = 0;
        i < min((b->size - sizeof(*b)) / sizeof(b->magics[0]), MAX_WRITES);
        i++)
        rassert(b->magics[i], ==, tid);
}

/* void mt_child_rand(int parent_tid); */

/* void malloc_test_randsize(){ */
/*     trace(); */
/*     int status = 0; */
/*     int ret = 0; */
/*     int i; */

/*     (void) ret; */
/*     (void) status; */

/*     pthread_t kids[NUM_MALLOC_TESTERS]; */
/*     for(i = 0; i < NUM_MALLOC_TESTERS; i++) */
/*         assert(!kfork((entrypoint_t *) mt_child_rand, */
/*                       (void *) _gettid(), KERN_ONLY)); */
    
/*     rdy = TRUE; */

/*     for(i = 0; i < NUM_MALLOC_TESTERS; i++) */
/*         assert(!pthread_join(kids[i], (void *[]){NULL})); */
/* } */

/* void mt_child_rand(int parent_tid){ */
/*     trace2(parent_tid, d); */
    
/*     list_t blocks = INITIALIZED_LIST; */
/*     int tid = _gettid(); */
/*     prand_init(); */

/*     while(rdy == FALSE) */
/*         _yield(parent_tid); */

/*     for(int i = 0; i < NUM_OPS; i++){ */
/*         int size; */
/*         int malloc_prob = */
/*             blocks.size < NUM_ALLOCATIONS/2 ? 75 : */
/*             blocks.size < NUM_ALLOCATIONS ? 50 : */
/*             blocks.size < NUM_ALLOCATIONS * 2 ? 25 : */
/*             0; */
        
/*         if(blocks.size > 0 && blocks.size % REPORT_INTERVAL == 0) */
/*             log2("i:%d allocated:%d", i, blocks.size); */

/*         if(rand_percent(malloc_prob)){ */
/*             size = prand() % (MAX_SIZE); */
/*             cur_block = wsmalloc(size); */
/*             if(!cur_block) */
/*                 continue; */
/*             *cur_block = (tblock_t) */
/*                 { . size = size, .lanc = INITIALIZED_LANCHOR }; */
/*             /\* Why doesn't this cause corruption if size isn't int aligned? *\/ */
/*             for(int *m = cur_block->magics; */
/*                 (uptr_t) m < (uptr_t) cur_block + */
/*                     umin(size, MAX_WRITES); */
/*                 m++) */
/*                 *m = tid; */
/*             list_add_rear(&cur_block->lanc, &blocks); */
/*         }else if(blocks.size){ */
/*             cur_block = */
/*                 lookup_lanchor(list_nth(prand() % blocks.size, */
/*                                         &blocks), */
/*                                tblock_t, lanc); */
/*             if(!cur_block) */
/*                 continue; */
/*             list_remove(&cur_block->lanc, &blocks); */
/*             for(int *m = cur_block->magics; */
/*                 (uptr_t) m < (uptr_t) cur_block + */
/*                     umin(cur_block->size, MAX_WRITES); */
/*                 m++) */
/*                 assert(*m == tid); */
/*             wsfree(cur_block, cur_block->size); */
/*         } */
/*     } */

/*     FOR_EACH_LLOOKUP(cur_block, struct block_t, lanc, &blocks){ */
/*         /\* Hacky way to make the iteration safe after freeing. *\/ */
/*         struct block_t local_copy = *cur_block; */
/*         wsfree(cur_block, cur_block->size); */
/*         cur_block = &local_copy; */
/*     } */

/*     report_profile(); */
/*     /\* exit(_gettid()); *\/ */
/* } */

void *mt_sharing_child();

struct sharing_child_args{
    int parent_tid;
    lfstack_t block_stacks[NUM_STACKS];
};

void malloc_test_sharing(){
    trace();

    struct sharing_child_args shared;
    shared.parent_tid = _gettid();
    for(int i = 0; i < NUM_STACKS; i++)
        shared.block_stacks[i] = (lfstack_t) INITIALIZED_STACK;

    pthread_t kids[NUM_MALLOC_TESTERS];
    for(int i = 0; i < NUM_MALLOC_TESTERS; i++)
        if(pthread_create(&kids[i], NULL, mt_sharing_child, (void *) &shared))
            LOGIC_ERROR("Failed to fork.");

    rdy = TRUE;

    for(int i = 0; i < NUM_MALLOC_TESTERS; i++)
        assert(!pthread_join(kids[i], (void *[]){NULL}));

    /* for(int i = 0; i < NUM_STACKS; i++){ */
    /*     lfstack_t *blocks = &shared.block_stacks[i]; */
    /*     tblock_t *cur_block; */
    /*     FOR_EACH_SPOP_LOOKUP(cur_block, tblock_t, sanc, blocks) */
    /*         wsfree(cur_block, cur_block->size); */
    /* } */
}

void *mt_sharing_child(struct sharing_child_args *shared){
    tblock_t *cur_block;

    int parent_tid = shared->parent_tid;
    int tid = _gettid();
    prand_init();

    while(rdy == FALSE)
        _yield(parent_tid);

    lfstack_t priv_blocks = INITIALIZED_STACK;
    size_t num_allocs = 0;
    for(int i = 0; i < NUM_OPS; i++){
        int size;
        lfstack_t *blocks= &shared->block_stacks[prand() % NUM_STACKS];
        int malloc_prob =
            num_allocs < NUM_ALLOCATIONS/2 ? 75 :
            num_allocs < NUM_ALLOCATIONS ? 50 :
            num_allocs < NUM_ALLOCATIONS * 2 ? 25 :
            0;

        if(rand_percent(malloc_prob)){
            size = min(MIN_SIZE + prand(),  MAX_SIZE);
            cur_block = wsmalloc(size);
            if(!cur_block)
                continue;
            log2("Allocated: %p", cur_block);
            *cur_block = (tblock_t)
                { .size = size, .sanc = INITIALIZED_SANCHOR };
            /* Try to trigger false sharing. */
            /* write_magics(cur_block, tid); */
            stack_push(&cur_block->sanc, blocks);
            num_allocs++;
        }else{
            cur_block =
                stack_pop_lookup(tblock_t, sanc, blocks);
            if(!cur_block)
                continue;
            log2("Claiming: %p", cur_block);
            /* write_magics(cur_block, tid); */
            stack_push(&cur_block->sanc, &priv_blocks);
        }

        if(rand_percent(50)){
            cur_block =
                stack_pop_lookup(tblock_t, sanc, &priv_blocks);
            if(!cur_block)
                continue;
            log2("Freeing priv: %p", cur_block);
            /* check_magics(cur_block, tid); */
            wsfree(cur_block, cur_block->size);
            num_allocs--;
        }
    }

    /* report_profile(); */
    /* exit(_gettid()); */
}

void *test(void *);

void hi(){
    trace2();
}

#include <time.h>
int main(){
    struct timespec start;

    hi();
    
    /* malloc_test(); */
    /* TIME(malloc_test_randsize()); */
    TIME(malloc_test_sharing());

    void *tst;
    TIME(tst = malloc(20));
    TIME(free(tst));
    
    struct timespec end;
    
    return 0;
}

