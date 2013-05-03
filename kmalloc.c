/**
 * @file   kmalloc.c
 * @author Alex Podolsky <apodolsk@andrew.cmu.edu>
 * 
 * @brief  Threadsafe wrappers for _malloc and friends.
 *
 * Macro trick: these C functions are later overshadowed by identically-named
 * macro wrappers which call them and then invoke SYSTEM_ERROR("OOM") upon
 * error. The point of using macros, rather than printing an error message
 * inside of these functions, is that the call to SYSTEM_ERROR will print the
 * name of the function which called them, rather than uselessly saying "OOM
 * in malloc()".
 */

#define MODULE KMALLOC

#include <pools.h>

#include <nalloc.h>

#include <global.h>

/* static mutex_t heap_lock = INITIALIZED_MUTEX; */

#define mutex_lock(...)
#define mutex_unlock(...)

void *(malloc)(size_t size)
{    
    void *ret;

    /* Depending on settings in global.h, this might give the kernel a
       workout by triggering artificial failures. */
    if(fail_randomly()) return NULL;

    mutex_lock(&heap_lock);
    ret = _nmalloc(size);
    mutex_unlock(&heap_lock);

    /* if(ret != NULL) */
    /*     leakchk_grab_bytes(size, ret); */
    
    return ret;    
}

void *(memalign)(size_t alignment, size_t size)
{
    void *ret;

    if(fail_randomly()) return NULL;

    mutex_lock(&heap_lock);
    ret = _nmemalign(alignment, size);
    mutex_unlock(&heap_lock);

    /* if(ret != NULL) */
    /*     leakchk_grab_bytes(size, ret); */
    
    return ret;
}

void *(calloc)(size_t nelt, size_t eltsize)
{
    void *ret;

    if(fail_randomly()) return NULL;
    
    mutex_lock(&heap_lock);
    ret = _ncalloc(nelt, eltsize);
    mutex_unlock(&heap_lock);

    /* if(ret != NULL) */
    /*     leakchk_grab_bytes(nelt * eltsize, ret); */
    
    return ret;    
}

void *(realloc)(void *buf, size_t new_size)
{
    void *ret;
    /* size_t size = leakchk_get_unknown_size(buf); */

    if(fail_randomly()) return NULL;
    
    mutex_lock(&heap_lock);
    ret = _nrealloc(buf, new_size);
    mutex_unlock(&heap_lock);

    /* if(ret != NULL){ */
    /*     leakchk_release_bytes(size, buf); */
    /*     leakchk_grab_bytes(new_size, ret); */
    /* } */
    
    return ret;
    
}

void (free)(void *buf)
{
    /* leakchk_release_bytes_unknown(buf); */
    mutex_lock(&heap_lock);
    _nfree(buf);
    mutex_unlock(&heap_lock);
}

void *(psmalloc)(size_t size, pool_t *pool)
{
    void *ret = NULL;

    if(fail_randomly()) return NULL;

    if(pool != NULL)
        ret = pool_get(pool);

    if(ret == NULL){
        mutex_lock(&heap_lock);
        ret = _nsmalloc(size);
        mutex_unlock(&heap_lock);
    }

    /* if(ret != NULL) */
    /*     leakchk_grab_bytes(size, ret); */
    
    return ret;    
}

void *(smalloc)(size_t size)
{
    void *ret;

    if(fail_randomly()) return NULL;

    mutex_lock(&heap_lock);
    ret = _nsmalloc(size);
    mutex_unlock(&heap_lock);

    /* if(ret != NULL) */
    /*     leakchk_grab_bytes(size, ret); */
    
    return ret;    
}

void *(psmemalign)(size_t alignment, size_t size, pool_t *pool){
    void *ret = NULL;

    if(fail_randomly()) return NULL;

    if(pool != NULL)
        ret = pool_get(pool);

    if(ret == NULL){
        mutex_lock(&heap_lock);
        ret = _nsmemalign(alignment, size);
        mutex_unlock(&heap_lock);
    }

    /* if(ret != NULL) */
    /*     leakchk_grab_bytes(size, ret); */

    assert((uintptr_t) ret % alignment == 0);
    
    return ret;    
}

void *(smemalign)(size_t alignment, size_t size)
{
    void *ret;

    if(fail_randomly()) return NULL;
    
    mutex_lock(&heap_lock);
    ret = _nsmemalign(alignment, size);
    mutex_unlock(&heap_lock);

    /* if(ret != NULL) */
    /*     leakchk_grab_bytes(size, ret);    */
    
    return ret;    
}

void *(scalloc)(size_t size)
{
    void *ret;

    if(fail_randomly()) return NULL;
    
    ret = smalloc(size);

    if(ret != NULL)
        memset(ret, 0, size);

    return ret;
}

void (psfree)(void *buf, size_t size, pool_t *pool){
    /* leakchk_release_bytes(size, buf); */
    
    if(pool != NULL && pool_put(buf, pool) < 0){
        mutex_lock(&heap_lock);
        _nsfree(buf, size);
        mutex_unlock(&heap_lock);
    }
}

void (sfree)(void *buf, size_t size)
{ 
    /* leakchk_release_bytes(size, buf); */
    
    mutex_lock(&heap_lock);
    _nsfree(buf, size);
    mutex_unlock(&heap_lock);
}
