/**
 * @file   wrappers.h
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * @date   Fri May  3 16:46:58 2013
 */

#ifndef WRAPPERS_H
#define WRAPPERS_H

#include <stdlib.h>
#include <nalloc.h>

#define malloc _nmalloc
#define free _nfree
#define smalloc _nsmalloc
#define sfree(ptr, size) _nsfree(ptr, size)

void report_profile(void);
void *wmalloc(size_t size);
void wfree(void *mem);
void *wsmalloc(size_t size);
void wsfree(void *mem, size_t size);

#endif
