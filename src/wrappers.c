/**
 * @file   timer_wrappers.c
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * @date   Fri May  3 16:24:07 2013
 * 
 */

#include <nalloc.h>
#include <stdlib.h>
#include <wrappers.h>
#include <global.h>

#define CAVG_BIAS .8

enum{ CAVG_MALLOC, CAVG_FREE };
static __thread struct{
    clock_t cavg;
    int count;
} profile[2];

void *wmalloc(size_t size){
    clock_t start = clock();
    void *out = malloc(size);
    clock_t fin = clock();
    profile[CAVG_MALLOC].cavg =
        profile[CAVG_MALLOC].cavg == 0 ? fin - start :
        profile[CAVG_MALLOC].cavg * CAVG_BIAS + (fin - start) * (1 - CAVG_BIAS);
    profile[CAVG_MALLOC].count++;
    return out;
}

void wfree(void *mem){
    clock_t start = clock();
    free(mem);
    clock_t fin = clock();
    profile[CAVG_FREE].cavg =
        profile[CAVG_FREE].cavg == 0 ? fin - start :
        profile[CAVG_FREE].cavg * CAVG_BIAS + (fin - start) * (1 - CAVG_BIAS);
    profile[CAVG_FREE].count++;
}

void *wsmalloc(size_t size){
    clock_t start = clock();
    void *out = smalloc(size);
    clock_t fin = clock();
    profile[CAVG_MALLOC].cavg =
        profile[CAVG_MALLOC].cavg == 0 ? fin - start :
        profile[CAVG_MALLOC].cavg * CAVG_BIAS + (fin - start) * (1 - CAVG_BIAS);
    profile[CAVG_MALLOC].count++;
    return out;
}

void wsfree(void *mem, size_t size){
    clock_t start = clock();
    sfree(mem, size);
    clock_t fin = clock();
    profile[CAVG_FREE].cavg =
        profile[CAVG_FREE].cavg == 0 ? fin - start :
        profile[CAVG_FREE].cavg * CAVG_BIAS + (fin - start) * (1 - CAVG_BIAS);
    profile[CAVG_FREE].count++;
}

void report_profile(void){
    log("Allocs: %d", profile[CAVG_MALLOC].count);
    log("Average clocks to alloc: %ld", profile[CAVG_MALLOC].cavg);
    
    log("Frees: %d", profile[CAVG_FREE].count);
    log("Average clocks to free: %ld", profile[CAVG_FREE].cavg);
}
