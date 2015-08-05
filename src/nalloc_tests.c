#define MODULE NALLOCTESTSM

#include <list.h>
#include <nalloc.h>
#include <stdlib.h>
#include <unistd.h>
#include <atomics.h>
#include <wrand.h>
#include <test_framework.h>

cnt max_allocs = 10000;
cnt niter = 1000000;
cnt max_writes = 0;
cnt max_size = 128;
bool print_profile = 0;

#define NOPS (niter / nthreads)

#define NSHARED_POOLS 32
#define NSHUFFLER_LISTS 16

#define MIN_SIZE (sizeof(tstblock))


/* Fall back to system malloc when compiling without nalloc.o for reference tests. */
__attribute__((__weak__))
void *(smalloc)(size s){
    return malloc(s);
}

__attribute__((__weak__))
void (sfree)(void *p, size s){
    free(p);
}

__attribute__((__weak__))
void linref_account_close(linref_account *a){
    (void) a;
}

__attribute__((__weak__))
void linref_account_open(linref_account *a){
    (void) a;
}

__attribute__((__weak__))
void nalloc_profile_report(void){}

void profile_report(void){
    nalloc_profile_report();
}

typedef struct{
    union{
        lanchor lanc;
        sanchor sanc;
    };
    size bytes;
    idx write_start;
    uint magics[];
} tstblock;

void write_magics(tstblock *b, uint magic){
    cnt nmagics = div_pow2(b->bytes - sizeof(*b), sizeof(b->magics[0]));
    for(idx i = b->write_start = nmagics ? (uint) wrand() % nmagics : 0;
        i < umin(max_writes, nmagics); i++)
        b->magics[i] = magic;
}

void check_magics(tstblock *b, uint magic){
    size max = umin(div_pow2(b->bytes - sizeof(*b), sizeof(b->magics[0])),
                    max_writes);
    for(uint i = b->write_start; i < max; i++)
        assert(b->magics[i] == magic);
}

void mt_child_rand(uint tid){
    thr_setup(tid);
    
    list blists[NSHUFFLER_LISTS];
    cnt allocs = 0;

    for(uint i = 0; i < NSHUFFLER_LISTS; i++)
        blists[i] = (list) LIST(&blists[i], NULL);

    thr_sync();

    for(uint i = 0; i < NOPS; i++){
        list *blocks = &blists[mod_pow2(wrand(), NSHUFFLER_LISTS)];
        if(randpcnt(allocs < max_allocs/2 ? 75 :
                    allocs < max_allocs ? 50 :
                    0))
        {
            /* size bytes = umax(MIN_SIZE, wrand() % (max_size)); */
            size bytes = 64;
            tstblock *b = smalloc(bytes);
            if(!b)
                continue;
            allocs++;
            /* ppl(0, b, blocks->size); */
            *b = (tstblock) {.bytes = bytes, .lanc = LANCHOR(NULL)};
            write_magics(b, tid);
            list_enq(&b->lanc, blocks);
        }else{
            tstblock *b = cof(list_deq(blocks), tstblock, lanc);
            if(!b)
                continue;
            allocs--;
            check_magics(b, tid);
            sfree(b, b->bytes);
        }
    }

    for(uint i = 0; i < NSHUFFLER_LISTS; i++)
        for(tstblock *b; (b = cof(list_deq(&blists[i]), tstblock, lanc));)
            sfree(b, b->bytes);
}

/* void mt_sharing_child(uint tid){ */
/*     thr_setup(tid); */

/*     sem_post(&kid_rdy); */
/*     sem_wait(&world_rdy); */

/*     list priv_blocks[NSHUFFLER_LISTS]; */
/*     for(uint i = 0; i < NSHUFFLER_LISTS; i++) */
/*         priv_blocks[i] = (list) LIST(&priv_blocks[i], NULL); */

/*     uint num_blocks = 0; */
/*     for(uint i = 0; i < NOPS; i++){ */
/*         int size; */
/*         lfstack *shared = &shared->block_stacks[wrand() % NSHARED_POOLS].s; */
/*         list *priv = &priv_blocks[wrand()  */
/*         int malloc_prob = */
/*             num_blocks < max_allocs/2 ? 75 : */
/*             num_blocks < max_allocs ? 50 : */
/*             num_blocks < 2 * max_allocs ? 25 : */
/*             0; */

/*         if(randpcnt(malloc_prob)){ */
/*             size bytes = umax(MIN_SIZE, wrand() % (max_size)); */
/*             b = smalloc(bytes); */
/*             if(!b) */
/*                 continue; */
/*             log(2, "Allocated: %", b); */
/*             *b = (tstblock) { .bytes = bytes, .sanc = SANCHOR }; */
/*             /\* Try to trigger false sharing. *\/ */
/*             write_magics(b, tid); */
/*             lfstack_push(&b->sanc, shared); */
/*             num_blocks++; */
/*         }else{ */
/*             b = */
/*                 cof(lfstack_pop(blocks), tstblock, sanc); */
/*             if(!b) */
/*                 continue; */
/*             log(2, "Claiming: %", b); */
/*             write_magics(b, tid); */
/*             b->lanc = (lanchor) LANCHOR(NULL); */
/*             list_enq(&b->lanc, &priv_blocks[wrand() % NSHUFFLER_LISTS]); */
/*         } */

/*         if(randpcnt(2 * (100 - malloc_prob))){ */
/*             b = cof(list_deq(&priv_blocks[wrand() % NSHUFFLER_LISTS]), */
/*                             tstblock, lanc); */
/*             if(!b) */
/*                 continue; */
/*             log(2, "Freeing priv: %", b); */
/*             check_magics(b, tid); */
/*             sfree(b, b->bytes); */
/*             num_blocks--; */
/*         } */
/*     } */

/*     profile_report(); */
/* } */

/* void producer_child(struct child_args *shared); */
/* void produce(struct child_args *shared); */

/* void producerest(void){ */
/*     struct child_args shared; */
/*     shared.parentid =get_dbg_id(); */
/*     for(uint i = 0; i < NSHARED_POOLS; i++) */
/*         shared.block_stacks[i].s = (lfstack) LFSTACK; */

/*     pthread_t kids[nthreads]; */
/*     for(uint i = 0; i < nthreads; i++) */
/*         if(kfork((entrypoint *) mt_sharing_child, &shared, KERN_ONLY) < 0) */
/*             EWTF("Failed to fork."); */

/*     rdy = true; */

/*     produce(&shared); */

/*     for(uint i = 0; i < nthreads; i++) */
/*         assert(!pthread_join(kids[i], (void *[]){NULL})); */

/*     for(uint i = 0; i < NSHARED_POOLS; i++){ */
/*         lfstack *blocks = &shared.block_stacks[i].s; */
/*         tstblock *b; */
/*         while((b = cof(lfstack_pop(blocks), tstblock, sanc))) */
/*             sfree(b, b->size); */
/*     } */
/* } */

/* void produce(struct child_args *shared){ */
/*     int tid =get_dbg_id(); */
/*     wsrand(rdtsc()); */

/*     stack priv_blocks = (stack) STACK; */
/*     tstblock *b; */
/*     uint num_blocks = 0; */
/*     for(uint i = 0; i < NOPS; i++){ */
/*         lfstack *shared= &shared->block_stacks[wrand() % NSHARED_POOLS].s; */
/*         int malloc_prob = */
/*             num_blocks < nthreads * max_allocs/2 ? 90 : */
/*             num_blocks < nthreads * max_allocs ? 70 : */
/*             num_blocks < nthreads * max_allocs * 2 ? 25 : */
/*             0; */

/*         if(randpcnt(malloc_prob)){ */
/*             cnt size = umax(MIN_SIZE, wrand() % (max_size)); */
/*             b = smalloc(size); */
/*             if(!b) */
/*                 continue; */
/*             log(2, "Allocated: %", b); */
/*             *b = (tstblock) { .bytes = size, .sanc = SANCHOR }; */
/*             /\* Try to trigger false sharing. *\/ */
/*             write_magics(b, tid); */
/*             lfstack_push(&b->sanc, shared); */
/*             num_blocks++; */
/*         }else { */
/*             b = cof(lfstack_pop(blocks), tstblock, sanc); */
/*             if(!b) */
/*                 continue; */
/*             log(2, "Claiming: %", b); */
/*             write_magics(b, tid); */
/*             stack_push(&b->sanc, &priv_blocks); */
/*             num_blocks--; */
/*         } */

/*         if(randpcnt(100 - malloc_prob)){ */
/*             b = */
/*                 cof(stack_pop(&priv_blocks), tstblock, sanc); */
/*             if(!b) */
/*                 continue; */
/*             log(2, "Freeing priv: %", b); */
/*             check_magics(b, tid); */
/*             sfree(b, b->size); */
/*         } */
/*     } */
    
/* } */

/* void consumer_child(struct child_args *shared){ */
/*     int parentid = shared->parentid; */
/*     int tid =get_dbg_id(); */
/*     tstblock *b; */
/*     wsrand(rdtsc()); */

/*     while(rdy == false) */
/*         _yield(parentid); */

/*     stack priv_blocks = STACK; */
/*     uint num_blocks = 0; */
/*     for(uint i = 0; i < NOPS; i++){ */
/*         lfstack *blocks= &shared->block_stacks[wrand() % NSHARED_POOLS].s; */
/*         int free_prob =  */
/*             num_blocks < max_allocs/2 ? 25 : */
/*             num_blocks < max_allocs ? 50 : */
/*             num_blocks < max_allocs * 2 ? 75 : */
/*             100; */

/*         if(randpcnt(free_prob)){ */
/*             b = */
/*                 cof(stack_pop(&priv_blocks), tstblock, sanc); */
/*             if(!b) */
/*                 continue; */
/*             log(2, "Freeing priv: %", b); */
/*             check_magics(b, tid); */
/*             sfree(b, b->size); */
/*             num_blocks--; */
/*         } else { */
/*             b = */
/*                 cof(lfstack_pop(blocks), tstblock, sanc); */
/*             if(!b) */
/*                 continue; */
/*             log(2, "Claiming: %", b); */
/*             write_magics(b, tid); */
/*             stack_push(&b->sanc, &priv_blocks); */
/*             num_blocks++; */
/*         } */


/*     } */

/*     profile_report(); */
/* } */

/* #define NBYTES (64000 * PAGE_SIZE) */
/* #define NBYTES 128 */
/* #define REPS 10000 */
/* volatile uptr update_mem[NBYTES]; */

/* void plain_update_kid(void){ */
/*     for(int r = 0; r < REPS; r++) */
/*         for(uint i = 0; i < NBYTES/sizeof(*update_mem); i++) */
/*             if(!update_mem[i]) */
/*                 update_mem[i] = 1; */
/* } */

/* void plain_update(void){ */
/*     pthread_t kids[nthreads]; */
/*     for(uint i = 0; i < nthreads; i++) */
/*         if(kfork((entrypoint *) plain_update_kid, NULL, KERN_ONLY) < 0) */
/*             EWTF("Failed to fork."); */
/* } */

/* void cas_update_kid(void){ */
/*     for(int r = 0; r < REPS; r++) */
/*         for(uint i = 0; i < NBYTES/sizeof(*update_mem); i++) */
/*             (void) cas((uptr) 1, &update_mem[i], (uptr) 0); */
/* } */

/* void cas_update(void){ */
/*     pthread_t kids[nthreads]; */
/*     for(uint i = 0; i < nthreads; i++) */
/*         if(kfork((entrypoint *) cas_update_kid, NULL, KERN_ONLY) < 0) */
/*             EWTF("Failed to fork."); */
/* } */

#define _MODULE HI
#define E__HI 1, 1, 1,

#define LOOKUP_ _LOOKUP(CONCAT(E_, _MODULE))
#define _LOOKUP_(mod) CONCAT2(_LOOKUP, NUM_ARGS(mod))(mod)
#define _LOOKUP1_(mod) 0
#define _LOOKUP3_(a, b, c) 1


int main(int argc, char **argv){
    int program = 1, opt;

    while( (opt = getopt(argc, argv, "t:a:i:w:p:")) != -1 ){
        switch (opt){
        case 't':
            nthreads = atoi(optarg);
            break;
        case 'a':
            max_allocs = atoi(optarg);
            break;
        case 'i':
            niter = atoi(optarg);
            break;
        case 'p':
            program = atoi(optarg);
            break;
        case 'w':
            max_writes = atoi(optarg);
            break;
        }
    }

    switch(program){
    case 1:
        launch_test((void *(*)(void*))mt_child_rand);
        break;
    }

    return 0;
}

