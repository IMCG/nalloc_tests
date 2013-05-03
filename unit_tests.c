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
#include <kmalloc.h>
#include <global.h>
#include <sys/wait.h>
#include <pebrand.h>

#define PAGE_SIZE 4096

typedef void *(entrypoint_t)(void *);


#define _yield(tid) pthread_yield()
#define _wait wait
#define exit(val) pthread_exit((void *) val)
#define kfork(entry, arg, flag)                           \
    pthread_create((pthread_t *){0}, NULL, entry, arg)    \

/* #define NUM_MALLOC_TESTERS 1000 */
#define NUM_MALLOC_TESTERS 40
#define NUM_ALLOCATIONS 1000
#define NUM_OPS 100 * NUM_ALLOCATIONS
#define REPORT_INTERVAL 100
#define SIZE1 12
#define SIZE2 16
static int rdy;
static pool_t pool1 = INITIALIZED_POOL(32);
static pool_t pool2 = INITIALIZED_POOL(32);

void mt_child(int parent_tid);

void malloc_test(void){
    trace();
    int status = 0;
    int ret = 0;
    int i;

    (void) ret;
    (void) status;
    (void) pool1;
    (void) pool2;
    
    for(i = 0; i < NUM_MALLOC_TESTERS; i++){
        if(kfork((entrypoint_t *) mt_child, (void *) _gettid(), KERN_ONLY) < 0)
            LOGIC_ERROR("Failed to fork.");
    }

    rdy = TRUE;

    pthread_exit(0);
}

#define test_free(x) psfree(x, size, size == SIZE1 ? &pool1 : &pool2)
#define test_malloc(x) psmalloc(x, size == SIZE1 ? &pool1 : &pool2)
/* #define test_free(x) sfree(x, size) */
/* #define test_malloc(x) smalloc(x) */

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

    exit(_gettid());
}

void mt_child2(int parent_tid);

void malloc_test_randsize(){
    trace();
    int status = 0;
    int ret = 0;
    int i;

    (void) ret;
    (void) status;
    (void) pool1;
    (void) pool2;
    
    for(i = 0; i < NUM_MALLOC_TESTERS; i++){
        if(kfork((entrypoint_t *) mt_child2, (void *) _gettid(), KERN_ONLY) < 0)
            LOGIC_ERROR("Failed to fork.");
    }

    rdy = TRUE;

    /* for(i = 0; i < NUM_MALLOC_TESTERS; i++){ */
    /*     assert((ret = _wait(&status)) > 0); */
    /*     assert(status == ret); */
    /* } */
}

void mt_child2(int parent_tid){
    list_t blocks = INITIALIZED_LIST;
    int jid = _gettid();
    
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
            size = pebrand() % (PAGE_SIZE/2);
            cur_block = smalloc(size);
            if(!cur_block)
                continue;
            *cur_block = (struct block_t)
                { . size = size, .lanc = INITIALIZED_LANCHOR };
            for(int *m = cur_block->magics;
                (uptr_t) m < (uptr_t) cur_block + size; m++)
                *m = jid;
            list_add_rear(&cur_block->lanc, &blocks);
        }else if(blocks.size){
            cur_block =
                lookup_lanchor(list_nth(pebrand() % blocks.size,
                                        &blocks),
                               struct block_t, lanc);
            if(!cur_block)
                continue;
            list_remove(&cur_block->lanc, &blocks);
            for(int *m = cur_block->magics;
                (uptr_t) m < (uptr_t) cur_block + cur_block->size; m++)
                assert(*m == jid);
            sfree(cur_block, cur_block->size);
        }
    }

    FOR_EACH_LLOOKUP(cur_block, struct block_t, lanc, &blocks){
        /* Hacky way to make the iteration safe after freeing. */
        struct block_t local_copy = *cur_block;
        free(cur_block);
        cur_block = &local_copy;
    }

    exit(_gettid());
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

    for(i = 0; i < NUM_MALLOC_TESTERS; i++){
        if(kfork((entrypoint_t *) mt_sharing_child, &shared, KERN_ONLY) < 0)
            LOGIC_ERROR("Failed to fork.");
    }

    rdy = TRUE;

    for(i = 0; i < NUM_MALLOC_TESTERS; i++){
        assert((ret = _wait(&status)) > 0);
        assert(status == ret);
    }

    for(int i = 0; i < NUM_STACKS; i++){
        lfstack_t *blocks = &shared.block_stacks[i];
        struct block_t *cur_block;
        FOR_EACH_SPOP_LOOKUP(cur_block, struct block_t, sanc, blocks)
            free(cur_block);
    }
}

void mt_sharing_child(
    struct{int parent_tid; lfstack_t block_stacks[NUM_STACKS];} *shared)
{
    int parent_tid = shared->parent_tid;
    int jid = _gettid();
    struct block_t *cur_block;

    while(rdy == FALSE)
        _yield(parent_tid);

    lfstack_t priv_blocks = INITIALIZED_STACK;
    for(int i = 0; i < NUM_OPS; i++){
        int size;
        lfstack_t *blocks= &shared->block_stacks[pebrand() % NUM_STACKS];
        int malloc_prob =
            blocks->size < NUM_ALLOCATIONS/2 ? 75 :
            blocks->size < NUM_ALLOCATIONS ? 50 :
            blocks->size < NUM_ALLOCATIONS * 2 ? 25 :
            0;
        
        if(blocks->size > 0 && blocks->size % REPORT_INTERVAL == 0)
            log2("i:%d allocated:%d", i, blocks->size);

        if(rand_percent(malloc_prob)){
            size = pebrand() % (PAGE_SIZE/2);
            cur_block = smalloc(size);
            if(!cur_block)
                continue;
            log("Allocated: %p", cur_block);
            *cur_block = (struct block_t)
                { .size = size, .sanc = INITIALIZED_SANCHOR };
            stack_push(&cur_block->sanc, blocks);
        }else if(blocks->size){
            cur_block =
                stack_pop_lookup(struct block_t, sanc, blocks);
            if(cur_block){
                log("Claiming: %p", cur_block);
                PINT(cur_block->size);
                for(int *m = cur_block->magics;
                    (uptr_t) m < (uptr_t) cur_block + cur_block->size; m++)
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
            log("Freeing priv: %p", cur_block);
            for(int *m = cur_block->magics;
                (uptr_t) m < (uptr_t) cur_block + cur_block->size; m++)
            {
                rassert(*m, ==, jid);
            }
            sfree(cur_block, cur_block->size);
        }
    }

    exit(_gettid());
}

int main(){

    /* if(!RUN_UNIT_TESTS){ */
    /*     done = TRUE; */
    /*     return; */
    /* } */
    
    trace();
    
    init_pebrand(clock());

    /* pebrand_test(); */
    /* probe_test(); */
    /* ret_addrs(); */
    /* aba_test(); */
    /* break_test(); */
    /* macro_test(); */
    /* malloc_test(); */
    /* malloc_test2(); */
    malloc_test_sharing();
    /* probe_kmem(); */
    /* cpuid_test(); */
    /* rwlock_test(); */
    /* rwlock_broken_test(); */
    /* lfstack_test(); */
    /* dbg_test(); */
    /* disk_test(); */
    /* lend_test(); */
    /* rand_fail_test(); */

    /* done = TRUE; */

    return 0;
}

