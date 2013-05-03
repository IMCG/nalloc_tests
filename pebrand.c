/**
 * @file   pebrand.c
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * 
 * @brief  Random number generator and some utilities that use it.
 * 
 */

#define MODULE PEBRAND

#include <pebrand.h>

#include <stdlib.h>

#include <global.h>

int rand_rdy = FALSE;
int always_fail = FALSE;

void init_pebrand(int seed){
    assert(!rand_rdy);
    
    rand_rdy = TRUE;
    srand(seed);
}

int pebrand_initialized(void){
    return rand_rdy;
}

unsigned long pebrand(void){
    return rand();
}

int timer_skip_randomly(void){
    if(RANDOM_TIMER_SKIP_PERCENT == 0) return 0;

    if(!(pebrand_initialized())) return 0;

    return pebrand() % 100 < RANDOM_TIMER_SKIP_PERCENT;
}

/** 
 * @brief Randomly return -1, or always return -1 if this thread is poisoned.
 *
 * Used by the frame and heap allocators to trigger artificial failures.
 */
int fail_randomly(void){
    if(RANDOM_FAIL_PER_THOUSAND == 0) return 0;

    if(!pebrand_initialized()) return 0;
        
    return -(pebrand() % 1000 < RANDOM_FAIL_PER_THOUSAND);
}

void pause_randomly(void){
    unsigned int start = _get_ticks();

    if(RANDOM_PAUSE_PERCENT == 0) return;

    if(!pebrand_initialized()) return;

    if(pebrand() % 100 < RANDOM_PAUSE_PERCENT){
        log("Pausing");
        while(start == _get_ticks()) continue;
        log("Done pausing.");
    }

    return;
}

int rand_percent(int p){
    unsigned long newr = pebrand() % 100;
    
    trace(newr, lu, p, d);
    
    return (newr) < p;
}
    
void rand_fail_test(){
    int j = 0;
    int saved_rdy = rand_rdy;
    rand_rdy = 1;
    for(int i = 0; i < 1000000; i++)
        if(fail_randomly())
            j++;
    _PINT(0,j);
    rand_rdy = saved_rdy;
}

