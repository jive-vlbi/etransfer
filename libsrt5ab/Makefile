# Makefile for libsrt5ab - adapted from libudt5ab for etransfer
# etd/etc's makefile calls this Makefile with
#    CPP=<whatever it is compiled with>
#    B2B=32|64|native  which bit-flavour etd/etc is compiled with
#    REPOS=path/to/arch-os-build specific location
# 
# NOTE: the original makefile had 'C++' but you can't, apparently,
#       pass that in on the commandline as "make C++=/path/to/compiler"
# The os setting is figured out by this Makefile itself
ifeq ($(REPOS),)
	SRTREPOS = "."
else
	SRTREPOS = ../$(REPOS)/libsrt5ab
endif

DBGFLAG=-O2
ifeq ($(BUILD),debug)
	override DBGFLAG+=-g -O0
endif

## Detect the operating system
UNAME=$(shell uname)
ifeq ($(UNAME),Linux)
	os     = LINUX
	target = $(SRTREPOS)/libsrt5ab.a
endif

ifeq ($(UNAME),Darwin)
	os     = OSX
	target = $(SRTREPOS)/libsrt5ab.a
endif

ifeq ($(UNAME),OpenBSD)
	os     = BSD
	target = $(SRTREPOS)/libsrt5ab.a
endif

ifeq ($(UNAME),FreeBSD)
	os     = BSD
	target = $(SRTREPOS)/libsrt5ab.a
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
	ifeq ($(MACHINE),arm64)
		arch=ARM64
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

# SRT version (without quotes for easier parsing)
SRT_VERSION = 1.5.4

SRT_VERSION_MAJOR := $(word 1, $(subst ., ,$(SRT_VERSION)))
SRT_VERSION_MINOR := $(word 2, $(subst ., ,$(SRT_VERSION)))
SRT_VERSION_PATCH := $(word 3, $(subst ., ,$(SRT_VERSION)))
SRT_VERSION_STRING := $(SRT_VERSION_MAJOR).$(SRT_VERSION_MINOR).$(SRT_VERSION_PATCH)

# SRT specific defines
SRT_DEFS = -DENABLE_JUMBO_FRAMES=1 -DENABLE_MONOTONIC_CLOCK=1 -DENABLE_STDCXX_SYNC=1

CPPFLAGS += -I$(SRTREPOS)
CCFLAGS = -fPIC -Wall -Wextra $(DBGFLAG) -D$(os) $(SRT_DEFS) -finline-functions -fno-strict-aliasing
CCFLAGS += -I. -Isrtcore -Ihaicrypt -I$(SRTREPOS)

CXXSTD = -std=c++11

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

ifeq ($(arch), ARM64)
   CCFLAGS += -DARM64
endif

# Linux needs -Wno-attributes for some gcc versions
ifeq ($(UNAME),Linux)
	CCFLAGS += -Wno-attributes
endif

SRT_CPP_SRCS = \
	api \
	buffer_rcv \
	buffer_snd \
	buffer_tools \
	cache \
	channel \
	common \
	congctl \
	core \
	crypto \
	epoll \
	fec \
	handshake \
	list \
	logger_default \
	logger_defs \
	logging \
	md5 \
	packet \
	packetfilter \
	queue \
	socketconfig \
	srt_c_api \
	strerror_defs \
	sync \
	sync_cxx11 \
	tsbpd_time \
	window

SRT_C_SRCS = srt_compat

SRT_OBJS = $(addprefix $(SRTREPOS)/, $(addsuffix .o, $(SRT_CPP_SRCS) $(SRT_C_SRCS)))

VERSION_H = $(SRTREPOS)/version.h

all: mkdir $(target) 

.PHONY: all libsrt5ab
libsrt5ab: all

$(VERSION_H): Makefile
	@ mkdir -p $(SRTREPOS)
	@ tmp=$$(mktemp $$PWD/version.h.XXXXXX); \
	{ \
		printf '/* Auto-generated, do not edit. */\n'; \
		printf '#ifndef INC_SRT_VERSION_H\n'; \
		printf '#define INC_SRT_VERSION_H\n'; \
		printf '#define SRT_MAKE_VERSION(major, minor, patch) \\\n'; \
		printf '   ((patch) + ((minor)*0x100) + ((major)*0x10000))\n'; \
		printf '#define SRT_MAKE_VERSION_VALUE SRT_MAKE_VERSION\n'; \
		printf '#define SRT_VERSION_MAJOR %s\n' '$(SRT_VERSION_MAJOR)'; \
		printf '#define SRT_VERSION_MINOR %s\n' '$(SRT_VERSION_MINOR)'; \
		printf '#define SRT_VERSION_PATCH %s\n' '$(SRT_VERSION_PATCH)'; \
		printf '#define SRT_VERSION_STRING "%s"\n' '$(SRT_VERSION_STRING)'; \
		printf '#ifndef SRT_VERSION\n'; \
		printf '#define SRT_VERSION SRT_VERSION_STRING\n'; \
		printf '#endif\n'; \
		printf '#define SRT_VERSION_VALUE \\\n'; \
		printf '   SRT_MAKE_VERSION_VALUE( \\\n'; \
		printf '      SRT_VERSION_MAJOR, SRT_VERSION_MINOR, SRT_VERSION_PATCH )\n'; \
		printf '#endif /* INC_SRT_VERSION_H */\n'; \
	} > $$tmp; \
	if ! cmp -s $$tmp $@; then mv $$tmp $@; else rm -f $$tmp; fi

define SRT_COMPILE_CPP


$(SRTREPOS)/$(1).o: srtcore/$(1).cpp srtcore/srt.h srtcore/srt_udt.h $(VERSION_H)
	@ mkdir -p $$(dir $$@)
	$(CPP) $(CPPFLAGS) $(CCFLAGS) $(CXXSTD) -o $$@ $$< -c
endef

define SRT_COMPILE_C

$(SRTREPOS)/$(1).o: srtcore/$(1).c srtcore/srt.h $(VERSION_H)
	@ mkdir -p $$(dir $$@)
	$(CC) $(CPPFLAGS) $(CCFLAGS) -o $$@ $$< -c
endef

$(foreach src,$(SRT_CPP_SRCS),$(eval $(call SRT_COMPILE_CPP,$(src))))
$(foreach src,$(SRT_C_SRCS),$(eval $(call SRT_COMPILE_C,$(src))))

$(SRTREPOS)/libsrt5ab.a: $(SRT_OBJS)
	ar -rcs $@ $^

clean:
	rm -f $(SRTREPOS)/*.o $(SRTREPOS)/*.so $(SRTREPOS)/*.dylib $(SRTREPOS)/*.a
	@echo "Cleaned $(SRTREPOS)"

mkdir:
	@ mkdir -p $(SRTREPOS)

.PHONY: failos failarch info
failos:
	@echo "Unrecognized/unset operating system!"; exit 1

failarch:
	@echo "Unrecognized/unset architecture!"; exit 1

info:
	@echo "$(UNAME) => os=$(os) arch=$(arch) target=$(target)"
	@echo " compiler=$(CPP) SRTREPOS=$(SRTREPOS)"
	@echo "MAKECMDGOALS=$(MAKECMDGOALS)"
