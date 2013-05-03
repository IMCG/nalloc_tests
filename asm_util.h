/**
 * @file   asm_util.h
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * 
 * @brief  Function prototypes for a variety of miscellanious
 * x86 assembly helper functions. 
 *
 * @note I'm unsure of the best way to prototype these functions, which
 * assume a certain operand size. It's cleaner to prototype the wrapper args
 * as "int", so that you keep the real operand size isolated inside the asm
 * files and can switch out just the asm if the size of "int" ever
 * changes. But then I think it might be even more useful to tie the C code
 * to a certain representation, consequently forcing a compiler error if the
 * representation ever changes - ie. force programmers to reconsider all the
 * places where a change in representation could *possibly* have an
 * unintended effect. Does this seem reasonable? 
 * 
 */

#ifndef __ASM_UTIL_H__
#define __ASM_UTIL_H__

#include <stdint.h>
#include <peb_macros.h>

COMPILE_ASSERT(sizeof(int) == 4);

/** 
 * @brief Wrapper for the locked version of the x86 xadd instruction.
 *
 * atomically:
 * int ret = *dest;
 * *dest += source;
 * return ret;
 *
 * 
 * @param source The amount by which to increment the contents of dest.
 * @param dest The address of an int which we will increment.
 * 
 * @return The value which dest contained immediately before the
 * increment. 
 */
int xadd(int source, int *dest);

/** 
 * @brief Atomically place the value of source in the memory pointed to by
 * dest, and then return the initial value of *dest.
 * 
 * @param source 
 * @param dest 
 * 
 * @return The initial value of *dest.
 */
int xchg(int source, int *dest);

/** 
 * @brief Wrapper for the x86 cmpxchg instruction.
 *
 * atomically:
 * if(*dest == expected_dest){
 *  *dest = source;
 *  return expected_dest;
 * } else {
 *  return *dest;
 * }
 * @param source The value to save into dest.
 * @param dest The address of an int which we will atomically compare
 * against expected_dest, and then replace with source.
 * @param expected_dest The value which we shall compare with *dest.
 * 
 * @return The initial value of *dest
 */
int cmpxchg(int source, int *dest, int expected_dest);

int64_t cmpxchg8b(int64_t src, int64_t *dest, int64_t expected_dest);


/** 
 * @brief Wrapper for the invlpg instruction. Flush the TLB entry for bad_page.
 * 
 * @param bad_page The address of the page whose mapping we need to remove
 * from the TLB.
 */
void invlpg(void *bad_page);


void xor_atomic(int src, int *dest);

/** 
 * @brief Return an address close to the current stack top. Useful for finding
 * out what stack you're on.
 * 
 */
void *approx_esp(void);

/** 
 * @brief Wrapper for hlt.
 * 
 */
void hlt(void);

typedef struct{
    int32_t eax;
    int32_t ebx;
    int32_t ecx;
    int32_t edx;
} cpuid_t;

/** 
 * Wrapper for cpuid.
 */
void cpuid(int eax, cpuid_t *buf);


#endif  /* __ASM_UTIL_H__ */
