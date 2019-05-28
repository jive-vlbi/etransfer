# Makefile from udt4 edited by HV to support compilation for etransfer
# etd/etc's makefile calls this Makefile with
#    CPP=<whatever it is compiled with>
#    B2B=32|64|native  which bit-flavour etd/etc is compiled with
#    REPOS=path/to/arch-os-build specific location
# 
# NOTE: the original makefile had 'C++' but you can't, apparently,
#       pass that in on the commandline as "make C++=/path/to/compiler"
# The os setting is figured out by this Makefile itself
ifeq ($(REPOS),)
	UDTREPOS = "."
else
	UDTREPOS = ../$(REPOS)/libudt5ab
endif

DBGFLAG=-O2
ifeq ($(BUILD),debug)
	override DBGFLAG+=-g -O0
endif

## Detect the operating system
UNAME=$(shell uname)
ifeq ($(UNAME),Linux)
	os     = LINUX
	target = $(UDTREPOS)/libudt5ab.a
endif

ifeq ($(UNAME),Darwin)
	os     = OSX
	target = $(UDTREPOS)/libudt5ab.a
endif

ifeq ($(UNAME),SunOS)
    os     = BSD
    target = $(UDTREPOS)/libudt5ab.a
endif

ifeq ($(os),)
	target=failos
endif

## Which architecture
ifeq ($(B2B),native)
	MACHINE=$(shell uname -m)
	ifeq ($(MACHINE),x86_64)
		arch=AMD64
	endif
	ifeq ($(MACHINE),i386)
		arch=IA32
	endif
endif

ifeq ($(B2B),32)
   arch = IA32
endif

ifeq ($(B2B),64)
	arch = AMD64
endif

ifeq ($(arch),)
	target=failarch
endif

CCFLAGS = -fPIC -Wall -Wextra $(DBGFLAG) -D$(os) -finline-functions -fno-strict-aliasing

ifeq ($(arch), IA32)
   CCFLAGS += -DIA32
endif

ifeq ($(arch), POWERPC)
   CCFLAGS += -mcpu=powerpc
endif

ifeq ($(arch), SPARC)
   CCFLAGS += -DSPARC
endif

ifeq ($(arch), IA64)
   CCFLAGS += -DIA64
endif

ifeq ($(arch), AMD64)
   CCFLAGS += -DAMD64
endif

OBJS = $(addprefix $(UDTREPOS)/, md5.o common.o window.o list.o buffer.o packet.o channel.o queue.o ccc.o cache.o core.o epoll.o api.o)

all: mkdir $(target) 

$(UDTREPOS)/%.o: %.cpp %.h udt.h
	$(CPP) $(CCFLAGS) -o $@ $< -c

$(UDTREPOS)/libudt5ab.a: $(OBJS)
	ar -rcs $@ $^

#libudt5ab.so: $(OBJS)
#	$(CPP) -shared -o $@ $^

#libudt5ab.dylib: $(OBJS)
#	$(CPP) -dynamiclib -o $@ -lstdc++ -lpthread -lm $^

clean:
	rm -f $(UDTREPOS)/*.o $(UDTREPOS)/*.so $(UDTREPOS)/*.dylib $(UDTREPOS)/*.a $(UDTREPOS)/udt
	@echo "Cleaned $(UDTREPOS)"

mkdir:
	@ mkdir -p $(UDTREPOS)

.PHONY: failos failarch compiling_it_wrong
failos:
	@echo "Unrecognized/unset operating system!"; exit 1

failarch:
	@echo "Unrecognized/unset architecture!"; exit 1

info:
	@echo "$(UNAME) => os=$(os) arch=$(arch) target=$(target)"
	@echo " compiler=$(CPP) UDTREPOS=$(UDTREPOS)"
	@echo "MAKECMDGOALS=$(MAKECMDGOALS)"
