#define MODULE NALLOCTESTSM

#include <list.h>
#include <nalloc.h>
#include <unistd.h>
#include <wrand.h>
#include <test_framework.h>

cnt max_allocs = 10000;
cnt niter = 10000000;
cnt max_writes = 0;
cnt max_size = 128;
bool print_profile = 0;
int program;

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
    uptr magics[];
} tstblock;

void write_magics(tstblock *b, uint magic){
    idx max = umin(div_pow2(b->bytes - sizeof(*b), sizeof(b->magics[0])),
                   max_writes);
    for(idx i = b->write_start = max ? rand() % max : 0; i < max; i++)
        b->magics[i] = magic;
}

void check_magics(tstblock *b, uint magic){
    size max = umin(div_pow2(b->bytes - sizeof(*b), sizeof(b->magics[0])),
                    max_writes);
    for(idx i = b->write_start; i < max; i++)
        assert(b->magics[i] == magic);
}

void private_pools_test(uint tid){
    list blists[NSHUFFLER_LISTS];
    cnt allocs = 0;

    for(uint i = 0; i < NSHUFFLER_LISTS; i++)
        blists[i] = (list) LIST(&blists[i], NULL);

    thr_sync(start_timing);

    for(uint i = 0; i < NOPS; i++){
        list *blocks = &blists[mod_pow2(rand(), NSHUFFLER_LISTS)];
        if(randpcnt(allocs < max_allocs/2 ? 75 :
                    allocs < max_allocs ? 50 :
                    0))
        {
            size bytes = umax(MIN_SIZE, rand() % (max_size));
            tstblock *b = smalloc(bytes);
            if(!b)
                continue;
            allocs++;
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

    thr_sync(stop_timing);
}

void shared_pools_test(uint tid){
    static lfstack shared[NSHARED_POOLS] = {[0 ... NSHARED_POOLS - 1] = LFSTACK};
    cnt allocs = 0;

    thr_sync(start_timing);

    for(uint i = 0; i < NOPS; i++){
        lfstack *s = &shared[mod_pow2(rand(), NSHARED_POOLS)];
        if(randpcnt(allocs < max_allocs/2 ? 75 :
                    allocs < max_allocs ? 50 :
                    0))
        {
            size bytes = umax(MIN_SIZE, rand() % (max_size));
            tstblock *b = smalloc(bytes);
            if(!b)
                continue;
            allocs++;
            *b = (tstblock) {.bytes = bytes, .lanc = LANCHOR(NULL)};
            write_magics(b, (uptr) b);
            lfstack_push(&b->sanc, s);
        }else{
            tstblock *b = cof(lfstack_pop(s), tstblock, lanc);
            if(!b)
                continue;
            allocs--;
            check_magics(b, (uptr) b);
            sfree(b, b->bytes);
        }
    }

    thr_sync(stop_timing);
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
/*         lfstack *shared = &shared->block_stacks[rand() % NSHARED_POOLS].s; */
/*         list *priv = &priv_blocks[rand()  */
/*         int malloc_prob = */
/*             num_blocks < max_allocs/2 ? 75 : */
/*             num_blocks < max_allocs ? 50 : */
/*             num_blocks < 2 * max_allocs ? 25 : */
/*             0; */

/*         if(randpcnt(malloc_prob)){ */
/*             size bytes = umax(MIN_SIZE, rand() % (max_size)); */
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
/*             list_enq(&b->lanc, &priv_blocks[rand() % NSHUFFLER_LISTS]); */
/*         } */

/*         if(randpcnt(2 * (100 - malloc_prob))){ */
/*             b = cof(list_deq(&priv_blocks[rand() % NSHUFFLER_LISTS]), */
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
/*         lfstack *shared= &shared->block_stacks[rand() % NSHARED_POOLS].s; */
/*         int malloc_prob = */
/*             num_blocks < nthreads * max_allocs/2 ? 90 : */
/*             num_blocks < nthreads * max_allocs ? 70 : */
/*             num_blocks < nthreads * max_allocs * 2 ? 25 : */
/*             0; */

/*         if(randpcnt(malloc_prob)){ */
/*             cnt size = umax(MIN_SIZE, rand() % (max_size)); */
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
/*         lfstack *blocks= &shared->block_stacks[rand() % NSHARED_POOLS].s; */
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

static void launch_nalloc_test(void *test, const char *name){
    launch_test(test, name);
    nalloc_profile_report();
}

int main(int argc, char **argv){
    int opt;

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
    case 0:
        launch_nalloc_test(private_pools_test, "private_pools_test");
        break;
    case 1:
        launch_nalloc_test(shared_pools_test, "shared_pools_test");
        break;
    }

    return 0;
}

