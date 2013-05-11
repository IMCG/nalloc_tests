# Note: This is all BS. I don't actually know make.
CC=clang
CFLAGS=-Wall -D_GNU_SOURCE -std=gnu99 -O3 -g -Isrc -Wno-int-to-pointer-cast -pthread -fno-omit-frame-pointer
LDFLAGS= -L/afs/cs/academic/class/15418-s13/public/lib -Xlinker -rpath -Xlinker /afs/cs/academic/class/15418-s13/public/lib -lprofiler -pthread -lrt
SRCDIR=src
OBJDIR=obj
SRCS=$(wildcard $(SRCDIR)/*.c $(SRCDIR)/*.S)
_OBJS=$(patsubst %.c,%.o,$(patsubst %.S,%.o,$(SRCS)))
OBJS=$(subst $(SRCDIR),$(OBJDIR),$(_OBJS))

all: utest natest libnalloc.so

-include $(OBJS:.o=.dep)

t-test: t-test1.c
	gcc -O3 -lpthread -o $@ $<

libnalloc.so: $(SRCS)
		$(CC) $(CFLAGS) -fPIC -shared -o $@ $^

# This is here so that I can set up a scheme where ONLY the test uses
# nalloc. Better for debugging, as you can't run gdb&co on top of a broken
# allocator.
natest: $(OBJS)
		$(CC) $(LDFLAGS) -o $@ $^

utest: $(filter-out obj/nalloc.o,$(OBJS))
		$(CC) $(LDFLAGS) -o $@ $^
$(OBJDIR)/%.o: $(SRCDIR)/%.c 
		$(CC) $(CFLAGS) -o $@ -c $<;
		gcc $(CFLAGS) -MM -MT $(OBJDIR)/$*.o -o $(OBJDIR)/$*.dep $^

$(OBJDIR)/%.o: $(SRCDIR)/%.S
		$(CC) $(CFLAGS) -o $@ -c $<

clean:
	rm $(OBJDIR)/*.o; 
	rm $(OBJDIR)/*.dep; 
	rm libnalloc.so; 
	rm utest;
