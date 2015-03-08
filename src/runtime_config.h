#define PAGE_SHIFT  12
#define PAGE_SIZE   (1 << PAGE_SHIFT)
#define CACHELINE_SIZE 64

#define HEAP_MAX 0x7ffff7fff000
#define HEAP_MIN 0x7fff00000000

#define SLAB_SIZE PAGE_SIZE
