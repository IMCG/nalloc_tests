CC=gcc
CFLAGS=-std=gnu99 -I. -g -O3 -Wno-int-to-pointer-cast -pthread -fno-omit-frame-pointer
LDFLAGS= -L/afs/cs/academic/class/15418-s13/public/lib -Xlinker -rpath -Xlinker /afs/cs/academic/class/15418-s13/public/lib -lprofiler -lrt -pthread
SRCS=$(wildcard *.c *.S)
OBJS=$(patsubst %.c,%.o,$(SRCS))
nalloc: $(OBJS)
		$(CC) $(LDFLAGS) -o $@ $^

%.o: %.C
		$(CC) $(CFLAGS) -c  $< 

clean:
	rm *.o 
