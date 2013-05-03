/**
 * @file   pebrand.h
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * 
 * @brief  
 * 
 */

#ifndef PEBRAND_H
#define PEBRAND_H

void init_pebrand(int seed);
int pebrand_initialized(void);

unsigned long pebrand(void);
int rand_percent(int p);

int timer_skip_randomly(void);
int fail_randomly(void);
void pause_randomly(void);


#endif
