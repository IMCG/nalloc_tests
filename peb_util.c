/**
 * @file   peb_util.c
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * 
 * @brief  A collection of misc. utility functions.
 */

#define MODULE UTIL

#include <global.h>

static int _dbg_preempt_enabled = TRUE;

inline ptrdiff_t ptrdiff(void *a, void *b){
    assert(a >= b);
    return (uintptr_t) a - (uintptr_t) b;
}

inline int in_kernel_mem(void *addr){
    return addr != NULL && (uintptr_t) addr < USER_MEM_START;
}

inline int in_user_mem(void *addr){
    return (uintptr_t) addr >= USER_MEM_START;
}

inline int interrupts_enabled(){
    return !!(get_eflags() & EFL_IF);
}

inline void strong_disable_interrupts(){
    trace3();
    assert(interrupts_enabled());
    disable_interrupts();
}

inline void strong_enable_interrupts(){
    trace3();
    assert(!interrupts_enabled());
    enable_interrupts();
}

inline int dbg_preempt_enabled(){
    return _dbg_preempt_enabled;
}

inline void dbg_enable_preempt(){    
    assert(!dbg_preempt_enabled());
    _dbg_preempt_enabled = TRUE;
}

inline void dbg_disable_preempt(){
    assert(dbg_preempt_enabled());
    _dbg_preempt_enabled = FALSE;
}

/** 
 * @brief Copy the string src to the buffer at dest. Dest must be large
 * enough to hold src, including its null byte.
 *
 * @return A pointer to the terminating null-byte of the newly copied string
 * inside dest.
 */
char* peb_stpcpy(char *dest, const char *src){
    while(*src != '\0')
        *dest++ = *src++;
    *dest = '\0';
    return dest;
}

/** 
 * @brief Find the length of a string, below a limit.
 *
 * The null character isn't included in the length, but it does count towards
 * the limit.
 * 
 * @return The length of the given string, or -1 if it's longer than max
 * chars.
 */
int strnlen(char *str, size_t max){
    trace2(str, p, max, d);
    for(int l = 0; l < max; l++)
        if(str[l] == '\0')
            return l;
    return -1;
}

char *itobs_8(int num, itobsbuf8_t *bin){
    trace2(num, d, bin, p);
    for(int i = 7; i >= 0; i--, num /= 2)
        (*bin)[i] = (num & 1) ? '1' : '0';
    (*bin)[8] = '\0';                 
    return *bin;
}    

char *itobs_16(int num, itobsbuf16_t *bin){
    trace2(num, d, bin, p);
    for(int i = 15; i >= 0; i--, num /= 2)
        (*bin)[i] = (num & 1) ? '1' : '0';
    (*bin)[16] = '\0';                 
    return *bin;
}    

char *itobs_32(int num, itobsbuf32_t *bin){
    trace2(num, d, bin, p);
    for(int i = 31; i >= 0; i--, num /= 2)
        (*bin)[i] = (num & 1) ? '1' : '0';
    (*bin)[32] = '\0';                 
    return *bin;
}    

/* I use the following functions when I need to fill in a function pointer
   with something generic. */

void report_err(){
    LOGIC_ERROR("Called a callback which we shouldn't call back.");
}

void no_op(){
    /* Poor no_op, with his deformed ear. I can understand why he looks so
       alarmed, what with all the people out to steal his lips and eyeball. */
    return;
}

int return_neg(){
    return -1;
}

void *return_null(){
    return NULL;
}

int return_zero(){
    return 0;
}

int return_zero_rare_event(){
    RARE_EVENT("I'm a surprising and rare callback.");
    return 0;
}
