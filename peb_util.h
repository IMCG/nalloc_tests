/**
 * @file   peb_util.h
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * 
 * @brief  Some globally useful utilities. 
 * 
 * See footnote about "zero" args.
 */

#ifndef __PEB_UTIL_H__
#define __PEB_UTIL_H__

#include <peb_macros.h>

typedef enum{
    CALLER_KERN = 0,
    CALLER_USER = 1,
} caller_type_t;


#define printf_ln(...)                                                  \
    printf(FIRST_ARG(__VA_ARGS__) "\n" COMMA_AND_TAIL_ARGS(__VA_ARGS__))

static inline int max(int a, int b){
    return a >= b ? a : b;
}
static inline int min(int a, int b){
    return a < b ? a : b;
}
#define umax(a, b) _umax((uintptr_t) a,(uintptr_t) b)
static inline uintptr_t _umax(uintptr_t a, uintptr_t b){
    return a >= b ? a : b;
}
#define umin(a, b) _umin((uintptr_t) a,(uintptr_t) b)
static inline uintptr_t _umin(uintptr_t a, uintptr_t b){
    return a < b ? a : b;
}

#define aligned(addr, size)                     \
    (((uintptr_t)(addr) % (size)) == 0)

#define align_down(addr, size)                              \
    ualign_down((uintptr_t) addr, size)

static inline uintptr_t ualign_down(uintptr_t addr, size_t size){
    return addr - addr % size;
}

#define align_up(addr, size)                    \
    (void *) ualign_up((uintptr_t) addr, size)

static inline uintptr_t ualign_up(uintptr_t addr, size_t size){
    return ualign_down(addr + size - 1, size);
}

#define uptr_add(addr, addend)                  \
    (uintptr_t) addr + addend

extern ptrdiff_t ptrdiff(void *a, void *b);

char *peb_stpcpy(char *dest, const char *src);

typedef char itobsbuf8_t[8 + 1];
char *itobs_8(int num, itobsbuf8_t *bin);
typedef char itobsbuf16_t[16 + 1];
char *itobs_16(int num, itobsbuf16_t *bin);
typedef char itobsbuf32_t[32 + 1];
char *itobs_32(int num, itobsbuf32_t *bin);

void report_err();
void no_op();
int return_neg();
void *return_null();
int return_zero();
int return_zero_rare_event();

#endif /* __PEB_UTIL_H__ */


