/**
 * @file   refcount.c
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * 
 * @brief  A basic refcount with an automatic destructor.
 *
 * In order to use this, you need a guarantee that, if you're about to destroy
 * the last reference to something, then no one else can create a new
 * reference.
 * 
 * @bugs None known.
 * 
*/

#define MODULE REFCOUNT

#include <refcount.h>
#include <asm_util.h>

#include <limits.h>

#include <global.h>

/** 
 * @brief Use the offset stored in 'class' to find the container of 'member'.
 */
static inline void *ref_container_of(refcount_t *member, const ref_class_t *class){
    trace(member, p, class->container_offset, d);
    return (uint8_t *) member - class->container_offset;
}

/** 
 * @brief Increment 'ref' by 'addend'.
 * 
 * @param class Only used for logging.
 */
void ref_up(refcount_t *refcount, int addend, const ref_class_t *class){
    trace(refcount, p, *refcount, d, addend, d, class, p);
    PPNT(ref_container_of(refcount, class));
    
    uint32_t old = xadd(addend, refcount);
                                                 
    (void) old;
    assert(old + addend > old );
    assert(old >= 0);
}

/** 
 * @brief Decrement 'ref' by 'subtrahend'. If 'ref' hits 0, then call
 * 'class''s destructor on the struct containing 'ref'.
 *
 * @param subtrahend The positive number to subtract from 'ref'. Truly a
 * lame-sounding word.
 */
void ref_down(refcount_t *refcount, int subtrahend, const ref_class_t *class){
    trace(refcount, p, *refcount, d, subtrahend, d, class, p);
    PPNT(ref_container_of(refcount, class));

    uint32_t old = xadd( -subtrahend, refcount);

    assert(old >= subtrahend && subtrahend > 0);

    if(old == subtrahend)
        class->destructor(ref_container_of(refcount, class));
}

uint32_t refcount(refcount_t *refcount){
    return *refcount;
}

