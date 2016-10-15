#define MODULE NALLOCTESTSM

#include <list.h>
#include <nalloc.h>
#include <unistd.h>
#include <wrand.h>
#include <test_framework.h>

#define NOPS (niter / nthreads)

#define NSHARED_POOLS 1
#define NSHUFFLER_LISTS 4

#define MIN_SIZE (sizeof(tstblock))

cnt max_allocs = 10000;
cnt niter = 10000000;
cnt max_writes = 1;
cnt max_size = 128;
bool print_profile = 0;
int program;

static lfstack shared[NSHARED_POOLS] = {[0 ... NSHARED_POOLS - 1] = LFSTACK};

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
    uptr magics[];
} tstblock;

void write_magics(tstblock *b, uint magic){
    idx max = umin(div_pow2(b->bytes - sizeof(*b), sizeof(b->magics[0])),
                   max_writes);
    for(idx i = 0; i < max; i++)
        b->magics[i] = magic;
}

void check_magics(tstblock *b, uint magic){
    idx max = umin(div_pow2(b->bytes - sizeof(*b), sizeof(b->magics[0])),
                    max_writes);
    for(idx i = 0; i < max; i++)
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
        if(randpcnt(!allocs ? 100 :
                    allocs < max_allocs/2 ? 75 :
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
            assert(b->bytes >= MIN_SIZE && b->bytes <= max_size);
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

typedef struct{
    void *b;
    idx gen;
} flent;
dbg used static __thread flent flog[16];
dbg static __thread idx flog_idx = 0;

void update_flog(void *b){
    flog[flog_idx++ % 16] = (flent){.b = b, .gen = shared[0].gen};
}

void shared_pools_test(uint tid){
    cnt allocs = 0;

    thr_sync(start_timing);

    for(uint i = 0; i < NOPS; i++){
        /* lfstack *s = &shared[mod_pow2(rand(), NSHARED_POOLS)]; */
        lfstack *s = &shared[0];
        if(randpcnt(!allocs ? 100 :
                    allocs < max_allocs/2 ? 75 :
                    allocs < max_allocs ? 50 :
                    0))
        {
            size bytes = umax(MIN_SIZE, rand() % (max_size));
            tstblock *b = smalloc(bytes);
            if(!b)
                continue;
            allocs++;
            *b = (tstblock) {.bytes = bytes, .sanc = SANCHOR};
            write_magics(b, (uptr) b);
            lfstack_push(&b->sanc, s);
        }else{
            tstblock *b = cof(lfstack_pop(s), tstblock, lanc);
            if(!b)
                continue;
            /* assert(b->bytes >= MIN_SIZE && b->bytes <= max_size); */
            if(!(b->bytes >= MIN_SIZE && b->bytes <= max_size)){
                ppl(0, b, b->bytes, b->magics[0], s);
                assert(0);
            }
                
            allocs--;
            check_magics(b, (uptr) b);
            update_flog(b);
            sfree(b, b->bytes);
        }
    }

    thr_sync(stop_timing);
}

static void launch_nalloc_test(void *test, const char *name){
    launch_test(test, name);
    for(idx i = 0; i < NSHARED_POOLS; i++)
        for(tstblock *b; (b = cof(lfstack_pop(&shared[i]), tstblock, sanc));)
            sfree(b, b->bytes);
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

