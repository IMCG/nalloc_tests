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
#include <time.h>
#include <global.h>
#include <unistd.h>

typedef void *(entrypoint_t)(void *);

#define _yield(tid) do{ (void) tid; pthread_yield();} while(0)
#define exit(val) pthread_exit((void *) val)
#define kfork(entry, arg, flag)                 \
    pthread_create(&kids[i], NULL, entry, arg)  \

#define wsmalloc(size) malloc(size)
#define wsfree(ptr, size) free(ptr)

static int num_threads = 1;
static int num_allocations = 20000;
static int ops_mult = 100;

#define NUM_OPS (ops_mult * num_allocations)
#define MAX_WRITES  MAX_SIZE
#define REPORT_INTERVAL 100

#define NUM_STACKS 1
#define NUM_LISTS 16

struct tblock_t{
    union{
        lanchor_t lanc;
        sanchor_t sanc;
    };
    int size;
    int magics[];
};

#define MAX_SIZE (1024)
#define MIN_SIZE (sizeof(struct tblock_t))

/* Fun fact: without the volatile, a test-yield loop on rdy will optimize out
   the test part and will just yield in a loop forever. Meanwhile
   if(0){expression;} generates an ASM branch that's never called. Nice job,
   GCC! */
static volatile int rdy;

static __thread unsigned int seed;
void prand_init(void){
    assert(read(open("/dev/urandom", O_RDONLY), &seed, sizeof(seed)) ==
           sizeof(seed));
}
long int prand(void){
    return rand_r(&seed);
}
int rand_percent(int per_centum){
    return prand() % 100 <= umin(per_centum, 100);
}

void write_magics(struct tblock_t *b, int tid){
    for(int *m = b->magics;
        (uptr_t) m < (uptr_t) b + umin(b->size - sizeof(*b), MAX_WRITES);
        m++)
        *m = tid;
}

void check_magics(struct tblock_t *b, int tid){
    for(int *m = b->magics;
        (uptr_t) m < (uptr_t) b + umin(b->size - sizeof(*b), MAX_WRITES);
        m++)
        rassert(*m, ==, tid);
}

/* Avoiding IFDEF catastrophe with the magic of weak symbols. */
/* __attribute__ ((weak)) */
/* nalloc_profile_t *get_profile(); */

void report_profile(void){
    /* if(get_profile){ */
    /*     nalloc_profile_t *prof = get_profile(); */
    /*     PUNT(prof->total_heap_highwater); */
    /*     PUNT(prof->num_arenas_highwater); */
    /* } */
}

void mt_child_rand(int parent_tid);

void malloc_test_randsize(){
    trace();

    pthread_t kids[num_threads];
    for(int i = 0; i < num_threads; i++)
        assert(!kfork((entrypoint_t *) mt_child_rand,
                      (void *) _gettid(), KERN_ONLY));
    
    rdy = TRUE;

    for(int i = 0; i < num_threads; i++)
        assert(!pthread_join(kids[i], (void *[]){NULL}));
}

void mt_child_rand(int parent_tid){
    trace2(parent_tid, d);

    struct tblock_t *cur_block;
    list_t block_lists[NUM_LISTS];
    int tid = _gettid();

    prand_init();
    for(int i = 0; i < NUM_LISTS; i++)
        block_lists[i] = (list_t) INITIALIZED_LIST;
    
    while(rdy == FALSE)
        _yield(parent_tid);

    for(int i = 0; i < NUM_OPS; i++){
        int size;
        list_t *blocks = &block_lists[prand() % NUM_LISTS];
        int malloc_prob =
            blocks->size < num_allocations/2 ? 75 :
            blocks->size < num_allocations ? 50 :
            blocks->size < num_allocations * 2 ? 25 :
            0;
        
        if(blocks->size > 0 && blocks->size % REPORT_INTERVAL == 0)
            log2("i:%d allocated:%d", i, blocks->size);

        if(rand_percent(malloc_prob)){
            size = umax(MIN_SIZE, prand() % (MAX_SIZE));
            cur_block = wsmalloc(size);
            if(!cur_block)
                continue;
            *cur_block = (struct tblock_t)
                { . size = size, .lanc = INITIALIZED_LANCHOR };
            write_magics(cur_block, tid);
            list_add_rear(&cur_block->lanc, blocks);
        }else if(blocks->size){
            cur_block = list_pop_lookup(struct tblock_t, lanc, blocks);
            if(!cur_block)
                continue;
            check_magics(cur_block, tid);
            wsfree(cur_block, cur_block->size);
        }
    }

    for(int i = 0; i < NUM_LISTS; i++){
        list_t *blocks = &block_lists[i];
        while((cur_block = list_pop_lookup(struct tblock_t, lanc, blocks)))
            wsfree(cur_block, cur_block->size);
    }

    report_profile();
    /* exit(_gettid()); */
}

void mt_sharing_child();

struct child_args{
    int parent_tid;
    lfstack_t block_stacks[NUM_STACKS];
};

void malloc_test_sharing(){
    trace();

    struct child_args shared;
    shared.parent_tid = _gettid();
    for(int i = 0; i < NUM_STACKS; i++)
        shared.block_stacks[i] = (lfstack_t) INITIALIZED_STACK;

    pthread_t kids[num_threads];
    for(int i = 0; i < num_threads; i++)
        if(kfork((entrypoint_t *) mt_sharing_child, &shared, KERN_ONLY) < 0)
            LOGIC_ERROR("Failed to fork.");

    rdy = TRUE;

    for(int i = 0; i < num_threads; i++)
        assert(!pthread_join(kids[i], (void *[]){NULL}));
}

void mt_sharing_child(struct child_args *shared){
    int parent_tid = shared->parent_tid;
    int tid = _gettid();
    struct tblock_t *cur_block;
    prand_init();

    while(rdy == FALSE)
        _yield(parent_tid);

    list_t priv_blocks[NUM_LISTS];
    for(int i = 0; i < NUM_LISTS; i++)
        priv_blocks[i] = (list_t) INITIALIZED_LIST;

    int num_blocks = 0;
    for(int i = 0; i < NUM_OPS; i++){
        int size;
        lfstack_t *blocks= &shared->block_stacks[prand() % NUM_STACKS];
        int malloc_prob =
            num_blocks < num_allocations/2 ? 75 :
            num_blocks < num_allocations ? 50 :
            num_blocks < 2 * num_allocations ? 25 :
            0;

        if(rand_percent(malloc_prob)){
            size = umax(MIN_SIZE, prand() % (MAX_SIZE));
            cur_block = wsmalloc(size);
            if(!cur_block)
                continue;
            log2("Allocated: %p", cur_block);
            *cur_block = (struct tblock_t)
                { .size = size, .sanc = INITIALIZED_SANCHOR };
            /* Try to trigger false sharing. */
            write_magics(cur_block, tid);
            stack_push(&cur_block->sanc, blocks);
            num_blocks++;
        }else{
            cur_block =
                stack_pop_lookup(struct tblock_t, sanc, blocks);
            if(!cur_block)
                continue;
            log2("Claiming: %p", cur_block);
            write_magics(cur_block, tid);
            cur_block->lanc = (lanchor_t) INITIALIZED_LANCHOR;
            list_add_front(&cur_block->lanc, &priv_blocks[prand() % NUM_LISTS]);
        }

        if(rand_percent(2 * (100 - malloc_prob))){
            cur_block = list_pop_lookup(struct tblock_t, lanc,
                                        &priv_blocks[prand() % NUM_LISTS]);
            if(!cur_block)
                continue;
            log2("Freeing priv: %p", cur_block);
            check_magics(cur_block, tid);
            wsfree(cur_block, cur_block->size);
            num_blocks--;
        }
    }

    report_profile();
}

void producer_child(struct child_args *shared);
void produce(struct child_args *shared);

void producer_test(void){
    trace();

    struct child_args shared;
    shared.parent_tid = _gettid();
    for(int i = 0; i < NUM_STACKS; i++)
        shared.block_stacks[i] = (lfstack_t) INITIALIZED_STACK;

    pthread_t kids[num_threads];
    for(int i = 0; i < num_threads; i++)
        if(kfork((entrypoint_t *) mt_sharing_child, &shared, KERN_ONLY) < 0)
            LOGIC_ERROR("Failed to fork.");

    rdy = TRUE;

    produce(&shared);

    for(int i = 0; i < num_threads; i++)
        assert(!pthread_join(kids[i], (void *[]){NULL}));

    for(int i = 0; i < NUM_STACKS; i++){
        lfstack_t *blocks = &shared.block_stacks[i];
        struct tblock_t *cur_block;
        FOR_EACH_SPOP_LOOKUP(cur_block, struct tblock_t, sanc, blocks)
            wsfree(cur_block, cur_block->size);
    }
}

void produce(struct child_args *shared){
    int tid = _gettid();
    prand_init();

    lfstack_t priv_blocks = (lfstack_t) INITIALIZED_STACK;
    struct tblock_t *cur_block;
    int num_blocks = 0;
    for(int i = 0; i < NUM_OPS; i++){
        int size;
        lfstack_t *blocks= &shared->block_stacks[prand() % NUM_STACKS];
        int malloc_prob =
            num_blocks < num_threads * num_allocations/2 ? 90 :
            num_blocks < num_threads * num_allocations ? 70 :
            num_blocks < num_threads * num_allocations * 2 ? 25 :
            0;

        if(rand_percent(malloc_prob)){
            size = umax(MIN_SIZE, prand() % (MAX_SIZE));
            cur_block = wsmalloc(size);
            if(!cur_block)
                continue;
            log2("Allocated: %p", cur_block);
            *cur_block = (struct tblock_t)
                { .size = size, .sanc = INITIALIZED_SANCHOR };
            /* Try to trigger false sharing. */
            write_magics(cur_block, tid);
            stack_push(&cur_block->sanc, blocks);
            num_blocks++;
        }else {
            cur_block =
                stack_pop_lookup(struct tblock_t, sanc, blocks);
            if(!cur_block)
                continue;
            log2("Claiming: %p", cur_block);
            write_magics(cur_block, tid);
            stack_push(&cur_block->sanc, &priv_blocks);
            num_blocks--;
        }

        if(rand_percent(100 - malloc_prob)){
            cur_block =
                stack_pop_lookup(struct tblock_t, sanc, &priv_blocks);
            if(!cur_block)
                continue;
            log2("Freeing priv: %p", cur_block);
            check_magics(cur_block, tid);
            wsfree(cur_block, cur_block->size);
        }
    }
    
}

void consumer_child(struct child_args *shared){
    int parent_tid = shared->parent_tid;
    int tid = _gettid();
    struct tblock_t *cur_block;
    prand_init();

    while(rdy == FALSE)
        _yield(parent_tid);

    lfstack_t priv_blocks = INITIALIZED_STACK;
    int num_blocks = 0;
    for(int i = 0; i < NUM_OPS; i++){
        lfstack_t *blocks= &shared->block_stacks[prand() % NUM_STACKS];
        int free_prob = 
            num_blocks < num_allocations/2 ? 25 :
            num_blocks < num_allocations ? 50 :
            num_blocks < num_allocations * 2 ? 75 :
            100;

        if(rand_percent(free_prob)){
            cur_block =
                stack_pop_lookup(struct tblock_t, sanc, &priv_blocks);
            if(!cur_block)
                continue;
            log2("Freeing priv: %p", cur_block);
            check_magics(cur_block, tid);
            wsfree(cur_block, cur_block->size);
            num_blocks--;
        } else {
            cur_block =
                stack_pop_lookup(struct tblock_t, sanc, blocks);
            if(!cur_block)
                continue;
            log2("Claiming: %p", cur_block);
            write_magics(cur_block, tid);
            stack_push(&cur_block->sanc, &priv_blocks);
            num_blocks++;
        }


    }

    report_profile();
}

int main(int argc, char **argv){
    unmute_log();

    int opt;
    while( (opt = getopt(argc, argv, "t:a:o:")) != -1 ){
        switch (opt){
        case 't':
            num_threads = atoi(optarg);
            break;
        case 'a':
            num_allocations = atoi(optarg);
            break;
        case 'o':
            ops_mult = atoi(optarg);
            break;
        }
    }

    /* expose_bug(); */
    /* TIME(malloc_test_randsize()); */
    TIME(malloc_test_sharing());
    /* TIME(producer_test()); */

    void *tst;
    TIME(tst = malloc(20));
    TIME(free(tst));

    return 0;
}

