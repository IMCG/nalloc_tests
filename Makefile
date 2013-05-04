CC=clang
CFLAGS=-I. -g -O3 -Wno-int-to-pointer-cast -pthread -fno-omit-frame-pointer
LDFLAGS= -lprofiler -pthread
SRCS=$(wildcard *.c *.S)
OBJS=$(patsubst %.c,%.o,$(SRCS))
nalloc: $(OBJS)
		$(CC) $(LDFLAGS) -o $@ $^

%.o: %.C
		$(CC) $(CFLAGS) -c  $< 

clean:
	rm *.o 
