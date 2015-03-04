#include <sys/mman.h>
#include <nalloc.h>

#define MAX_REALIGNS 16

CASSERT(SLAB_SIZE == PAGE_SIZE);
struct slab *new_slabs(cnt batch){
    struct slab *s = mmap(NULL, SLAB_SIZE * batch, PROT_WRITE,
                          MAP_PRIVATE | MAP_POPULATE | MAP_ANONYMOUS, -1, 0);
    pp(s);
    return s == MAP_FAILED ? EOOR(), NULL : s;
    
}

#include <pthread.h>
#include <thread.h>
__thread thread manual_tls;

struct thread *this_thread(void){
    return (struct thread *) pthread_self();
}

__thread uint _dbg_id;
dbg_id get_dbg_id(void){
    return _dbg_id;
}

void set_dbg_id(dbg_id id){
    _dbg_id = id;
}

bool interrupts_enabled(void){
    return true;
}

err kyield(tid t){
    assert(t == -1);
    pthread_yield();
    return 0;
}

void *heap_start(){
    TODO();
    return NULL;
    /* extern void *end; */
    /* return &end; */
}

/* TODO: /proc/self/nonsense */
void *heap_end(){
    return (void *) 0x0101010101010101;
}

#include <stdio.h>
inline
void breakpoint(void){
    fflush(stdout);
    asm volatile("int $3;");
}

void panic(const char *_, ...){
    breakpoint();
    abort();
}

bool poisoned(void){
    return false;
}
