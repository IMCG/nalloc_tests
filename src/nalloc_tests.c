#define MODULE NALLOCTESTSM

#include <list.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <nalloc.h>
#include <stdlib.h>
#include <time.h>
#include <atomics.h>
#include <unistd.h>
#include <wrand.h>
#include <asm.h>
#include <timing.h>
#include <semaphore.h>

typedef void *(entrypoint)(void *);

cnt nthreads = 100;
cnt max_allocs = 1000;
cnt niter = 10000;
cnt max_writes = 8;
cnt max_size = 128;
bool print_profile = 0;

#define NOPS (niter / nthreads)

#define NSHARED_POOLS 32
#define NSHUFFLER_LISTS 16

#define MIN_SIZE (sizeof(tstblock))

#define CAVG_BIAS .05
static __thread double malloc_cavg;
static __thread double free_cavg;

struct child_args{
    int parentid;
    struct {
        align(CACHELINE_SIZE)
        lfstack s;
    } block_stacks[NSHARED_POOLS];
};

/* GDB starts counting threads at 1, so the first child is 2. Urgh. */
const uptr firstborn = 2;

static sem_t world_rdy;
static sem_t kid_rdy;

static volatile struct tctxt{
    pthread_t id;
    bool dead;
} *threads;
static sem_t unpauses;
static sem_t pauses;
static pthread_mutex_t state_lock = PTHREAD_MUTEX_INITIALIZER;

/* To be used when compiling with a reference malloc. */
__attribute__((__weak__))
void *smalloc(size s){
    return malloc(s);
}

__attribute__((__weak__))
void sfree(void *p, size s){
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

static void thr_setup(uint id){
    set_dbg_id(id);
    wsrand(TIME());
    muste(pthread_mutex_lock(&state_lock));
    threads[id - firstborn] = (struct tctxt) {pthread_self()};
    muste(pthread_mutex_unlock(&state_lock));
}

static void thr_destroy(uint id){
    muste(pthread_mutex_lock(&state_lock));
    threads[id - firstborn].dead = true;
    muste(pthread_mutex_unlock(&state_lock));    
}

uptr waiters;
err pause_universe(void){
    assert(waiters < nthreads);
    if(!cas_won(1, &waiters, (iptr[]){0}))
        return -1;
    muste(pthread_mutex_lock(&state_lock));
    cnt live = 0;
    for(volatile struct tctxt *c = &threads[0]; c != &threads[nthreads]; c++)
        if(!c->dead && c->id != pthread_self()){
            live++;
            pthread_kill(c->id, SIGUSR1);
        }
    waiters += live;
    muste(pthread_mutex_unlock(&state_lock));
    for(uint i = 0; i < live; i++)
        muste(sem_wait(&pauses));
    return 0;
}

void resume_universe(void){
    cnt live = 0;
    for(volatile struct tctxt *c = &threads[0]; c != &threads[nthreads]; c++)
        if(!c->dead && c->id != pthread_self())
            live++;
    assert(live == waiters - 1);
    for(cnt i = 0; i < live; i++)
        muste(sem_post(&unpauses));
    xadd(-1, &waiters);
}

void wait_for_universe(){
    muste(sem_post(&pauses));
    muste(sem_wait(&unpauses));
    xadd(-1, &waiters);
}

static void launch_test(void *test(void *)){
    muste(sem_init(&pauses, 0, 0));
    muste(sem_init(&unpauses, 0, 0));
    muste(sem_init(&world_rdy, 0, 0));
    muste(sem_init(&kid_rdy, 0, 0));
    muste(sigaction(SIGUSR1,
                    &(struct sigaction){.sa_handler=wait_for_universe,
                            .sa_flags=SA_RESTART | SA_NODEFER}, NULL));

    struct tctxt threadscope[nthreads];
    memset(threadscope, 0, sizeof(threadscope));
    threads = threadscope;
    for(uint i = 0; i < nthreads; i++)
        if(pthread_create((pthread_t *) &threads[i].id, NULL,
                          (void *(*)(void*))test,
                          (void *) (firstborn + i)))
            EWTF();
    for(uint i = 0; i < nthreads; i++)
        sem_wait(&kid_rdy);
    for(uint i = 0; i < nthreads; i++)
        sem_post(&world_rdy);

    timeval start = get_time();
    for(uint i = 0; i < nthreads; i++)
        pthread_join(threads[i].id, NULL);
    ppl(0, time_diff(start));
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

void profile_init(void){
    void *ptr;
    if(!print_profile)
        return;
    malloc_cavg = TIME(ptr = malloc(42));
    assert(ptr);
    free(ptr);
}
        
void profile_report(void){}

void *wsmalloc(size s){
    void *found;
    if(print_profile)
        malloc_cavg = CAVG_BIAS * TIME(found = smalloc(s))
            + (1 - CAVG_BIAS) * malloc_cavg;
    else
        found = smalloc(s);
    return found;
}

void wsfree(void *ptr, size s){
    if(print_profile)
        free_cavg = CAVG_BIAS * TIME(free(ptr))
            + (1 - CAVG_BIAS) * free_cavg;
    else
        sfree(ptr, s);
}

void mt_child_rand(uint tid){
    thr_setup(tid);
    
    list blists[NSHUFFLER_LISTS];

    for(uint i = 0; i < NSHUFFLER_LISTS; i++)
        blists[i] = (list) LIST(&blists[i], NULL);

    sem_post(&kid_rdy);
    sem_wait(&world_rdy);

    for(uint i = 0; i < NOPS; i++){
        list *blocks = &blists[mod_pow2(wrand(), NSHUFFLER_LISTS)];
        if(randpcnt((blocks->size < max_allocs/2) ? 75 :
                    (blocks->size < max_allocs) ? 50 :
                    (blocks->size < max_allocs * 2) ? 25 :
                    0))
        {
            size bytes = umax(MIN_SIZE, wrand() % (max_size));
            tstblock *b = wsmalloc(bytes);
            if(!b)
                continue;
            pp(b);
            *b = (tstblock) {.bytes = bytes, .lanc = LANCHOR(NULL)};
            write_magics(b, tid);
            list_enq(&b->lanc, blocks);
        }else{
            tstblock *b = cof(list_deq(blocks), tstblock, lanc);
            if(!b)
                continue;
            check_magics(b, tid);
            wsfree(b, b->bytes);
        }
    }

    for(uint i = 0; i < NSHUFFLER_LISTS; i++)
        for(tstblock *b; (b = cof(list_deq(&blists[i]), tstblock, lanc));)
            wsfree(b, b->bytes);
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
/*             b = wsmalloc(bytes); */
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
/*             wsfree(b, b->bytes); */
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
/*             wsfree(b, b->size); */
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
/*             b = wsmalloc(size); */
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
/*             wsfree(b, b->size); */
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
/*             wsfree(b, b->size); */
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

int main(int argc, char **argv){
    profile_init();

    int program = 1, opt, do_malloc = 0;
    while( (opt = getopt(argc, argv, "t:a:i:p:w")) != -1 ){
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

