/**
 * @file   kmalloc.h
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * @date   Sat Oct  6 02:31:14 2012
 *
 * Automatically print an error message upon malloc() failure, with __func__
 * and __LINE__ in the logger call taking on values as if malloc()'s caller
 * had called SYSTEM_ERROR("OOM") themselves.
 */

#ifndef KMALLOC_H
#define KMALLOC_H

#include <malloc.h>
#include <pools.h>
#include <peb_macros.h>

void *malloc(size_t size);
void *memalign(size_t alignment, size_t size);
void *calloc(size_t nelt, size_t eltsize);
void *realloc(void *buf, size_t new_size);
void free(void *buf);
void *smalloc(size_t size);
void *smemalign(size_t alignment, size_t size);
void sfree(void *buf, size_t size);

void *psmalloc(size_t size, pool_t *pool);
void *psmemalign(size_t alignment, size_t size, pool_t *pool);
void psfree(void *buf, size_t size, pool_t *pool);

#include <log.h>
#ifdef __GNUC__
/* Can't use static inline to avoid double-eval. Can't use GCC comma extension
   because SYSTEM_ERROR uses do-while. Instead, using GCC ({}) where last
   statement becomes value of expression.

   Using of FIRST_ARG &co. so that I can use first arg twice without
   evaluating it twice. Consider free(arr_of_pointers[i--]) or
   malloc(update_size()). Could use typeof rather than assuming first arg is
   an int, but then typeof allows side effects in VLAs. */
#define log_wrapper(func, ...)                                          \
    ({                                                                  \
        int _log_tmp_first_arg = FIRST_ARG(__VA_ARGS__);                \
        void *_log_wrapper_tmp = func(_log_tmp_first_arg                \
                                        COMMA_AND_TAIL_ARGS(__VA_ARGS__)); \
        if(!_log_wrapper_tmp)                                           \
            SYSTEM_ERROR("OOM");                                        \
        __log(LOG_KMALLOC, 1, "%s:%p:%d:%s:%d",                         \
              #func,                                                    \
              _log_wrapper_tmp,                                         \
              _log_tmp_first_arg,                                       \
              __func__,                                                 \
              __LINE__                                                  \
            );                                                          \
       _log_wrapper_tmp;                                                \
    })
#else
#define log_wrapper(func, ...)                  \
    func(__VA_ARGS__)
#endif

#define vlog_wrapper(func, ...)                                     \
    do{                                                             \
        void *_log_tmp_first_arg = FIRST_ARG(__VA_ARGS__);          \
        __log(LOG_KMALLOC, 1, "%s:%p:%s:%d",                        \
              #func,                                                \
              _log_tmp_first_arg,                                   \
              __func__,                                             \
              __LINE__);                                            \
        func(_log_tmp_first_arg COMMA_AND_TAIL_ARGS(__VA_ARGS__));  \
    }while(0);                                  


/* Since macros don't expand themselves recursively, each of these will just
   pass the name of its corresponding C function to log_wrapper. */
#define psmalloc(...) log_wrapper(psmalloc, __VA_ARGS__)
#define psmemalign(...) log_wrapper(psmemalign, __VA_ARGS__)

#define malloc(...) log_wrapper(malloc, __VA_ARGS__)
#define memalign(...) log_wrapper(memalign, __VA_ARGS__)
#define calloc(...) log_wrapper(calloc, __VA_ARGS__)
#define realloc(...) log_wrapper(realloc, __VA_ARGS__)
#define smalloc(...) log_wrapper(smalloc, __VA_ARGS__)
#define smemalign(...) log_wrapper(smemalign, __VA_ARGS__)
#define scalloc(...) log_wrapper(scalloc, __VA_ARGS__)

#define psfree(...) vlog_wrapper(psfree, __VA_ARGS__)
#define free(...) vlog_wrapper(free, __VA_ARGS__)
#define sfree(...) vlog_wrapper(sfree, __VA_ARGS__)



#endif
