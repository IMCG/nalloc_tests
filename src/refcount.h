/**
 * @file   refcount.h
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * 
 * @brief  Internal and external refcount declarations.
 * 
 */

#ifndef __PEB_REFCOUNT_H__
#define __PEB_REFCOUNT_H__

#include <stdint.h>

typedef int refcount_t;
#define FRESH_REFCOUNT(nrefs) (nrefs)

typedef struct{
    void (*destructor)(void *); /**< Called upon the struct containing a refcount_t
                                   when that ref reaches 0. */
    int container_offset;  /**< Used to find the container of a refcount_t. */
} ref_class_t;

void ref_up(refcount_t *refcount, int to_add, const ref_class_t *class);
void ref_down(refcount_t *refcount, int to_subtract, const ref_class_t *class);
uint32_t refcount(refcount_t *refcount);

#endif  /* __PEB_REFCOUNT_H__ */
