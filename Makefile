BUILTIN_VARS:=$(.VARIABLES)
CC:=clang
SRCD:=src
OBJD:=obj
INC:=$(shell find -L $(SRCD) -not -path "*/.*" -type d | sed s/^/-I/)
HDRS:=$(shell find -L $(SRCD) -type f -name *.h)
SRCS_C:=$(shell find -L $(SRCD) -type f -name *.c)
SRCS_S:=$(shell find -L $(SRCD) -type f -name *.S)
SRCS:=$(SRCS_C) $(SRCS_S)
OBJS:=$(subst $(SRCD),$(OBJD),$(patsubst %.c,%.o,$(patsubst %.S,%.o,$(SRCS))))
DIRS:=$(shell echo $(dir $(OBJS)) | tr ' ' '\n' | sort -u | tr '\n' ' ')
CFLAGS:=$(INC)\
	-std=gnu11\
	-fms-extensions\
	-include "dialect.h"\
	-O3 \
	-flto\
	-g\
	-D_GNU_SOURCE\
	-Wall \
	-Wextra \
	-Werror \
	-Wcast-align\
	-Wno-missing-field-initializers \
	-Wno-ignored-qualifiers \
	-Wno-missing-braces \
	-Wno-unused-parameter \
	-Wno-unused-function\
	-Wno-unused-value\
	-Wno-address\
	-Wno-unused-variable\
	-Wno-microsoft-anon-tag\
	-Wno-unknown-pragmas\
	-pthread\
	-march=native\
	-mtune=native\
	-m64\
	-mcx16\

LD:=$(CC)
LDFLAGS:=-fvisibility=hidden -lprofiler $(CFLAGS)

all: test pt_ref je_ref tc_ref ll_ref

test: $(DIRS) $(SRCD)/TAGS $(OBJS) Makefile
	+ $(LD) $(LDFLAGS) -o $@ $(OBJS)

TEST_OBJS=$(filter-out $(OBJD)/nalloc/nalloc.o, $(OBJS))
pt_ref: $(DIRS) $(SRCD)/TAGS $(TEST_OBJS) Makefile
	+ $(LD) $(LDFLAGS) -o $@ $(TEST_OBJS)

je_ref: $(DIRS) $(SRCD)/TAGS $(TEST_OBJS) Makefile
	+ $(LD) $(LDFLAGS) -ljemalloc -o $@ $(TEST_OBJS)

tc_ref: $(DIRS) $(SRCD)/TAGS $(TEST_OBJS) Makefile
	+ $(LD) $(LDFLAGS) -ltcmalloc -o $@ $(TEST_OBJS)

# ll_ref: $(DIRS) $(SRCD)/TAGS $(TEST_OBJS) Makefile
# 	+ $(LD) $(LDFLAGS) -o $@ $(TEST_OBJS) ./libllalloc.so.1.3

$(DIRS):
	mkdir -p $@

$(SRCD)/TAGS: $(SRCS) $(HDRS)
	etags -o $(SRCD)/TAGS $(HDRS) $(SRCS)

$(OBJS): Makefile

$(OBJD)/%.o: $(SRCD)/%.c
		$(CC) $(CFLAGS) -MM -MP -MT $(OBJD)/$*.o -o $(OBJD)/$*.dep $<
		$(CC) $(CFLAGS) -o $@ -c $<;

$(OBJD)/%.o: $(SRCD)/%.S
		$(CC) $(CFLAGS) -MM -MP -MT $(OBJD)/$*.o -o $(OBJD)/$*.dep $<
		$(CC) $(CFLAGS) -o $@ -c $<

-include $(OBJS:.o=.dep)

check-syntax:
		$(CC) $(CFLAGS) -c $(CHK_SOURCES) -o /dev/null

clean:
	rm -rf $(OBJD) 
	rm $(SRCD)/TAGS
	rm -f test;

define \n


endef
info::
	$(foreach v, \
		$(filter-out $(BUILTIN_VARS) BUILTIN_VARS \n, $(.VARIABLES)), \
		$(info $(v) = $($(v)) ${\n}))
