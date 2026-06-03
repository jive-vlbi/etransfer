# if not overridden on cmd line set to native
B2B?=native
DEFAULTTARGETS=etc etd
CC=gcc
CXX=g++
LD=$(CXX) $(LDOPT)
BINDIR=/usr/local/bin
BUILDINFO=$(shell hostname; echo ":"; date '+%d-%b-%Y : %Hh%Mm%Ss' )
DATE=$(shell date '+%d-%b-%Y %Hh%Mm%Ss')

# NOTE: -Wconversion is intentionally *not* enabled. It produces a flood
# of -Wsign-conversion noise at every POSIX I/O boundary (read/write
# returning ssize_t, fed into size_t buffers) where the typical "fix"
# is a static_cast<size_t>() that hides nothing and catches nothing.
# See docs/checked-conversions-plan.md for a sketch of how we'd revisit
# this with throwing checked conversions (etdc::narrow) if we ever want
# real bounds-checking at those boundaries.
BASEOPT=-fPIC $(OPT) -Wall -W -Werror -Wextra -pedantic -pedantic-errors -DB2B=$(B2B) -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D__STDC_FORMAT_MACROS -Wcast-qual -Wwrite-strings -Wredundant-decls -Wfloat-equal -Wshadow -Wundef -D_FILE_OFFSET_BITS=64

CCOPT=$(BASEOPT) -Wbad-function-cast -Wstrict-prototypes
CXXOPT=$(BASEOPT) -std=c++11 

ifeq ($(shell uname),OpenBSD)
	# OpenBSD system headers redeclare the same stuff many times ...
	override CXXOPT += -Wno-redundant-decls
endif
ifeq ($(B2B),32)
	override CC    += -m32
	override CXX   += -m32 -ansi
	override LDOPT += -m32 -fPIC
	override CCOPT += -malign-double
endif
ifeq ($(B2B),64)
	override CC    += -m64
	override CXX   += -m64 -ansi
	override LDOPT += -m64 -fPIC
endif

# DEBUG=1 => do debug compile
#  Unfortunately there's a macro in the code called "DEBUG(...)"
#  so we can't compile with "-DDEBUG=1". That was a stupid idea of 
#  me to name the macro that simple ...
ifneq ($(strip $(DEBUG)),)
	OPT   += -g -DGDBDEBUG=1 -O0
	ASOPT += -g
	BUILD =  debug
else
	OPT   += -O2
	BUILD =  opt
endif

#### The include path(s)
INCD+=-I$(shell pwd)/src -I$(shell pwd)/argparse11

PLATFORMLIBS=
ifeq ($(shell uname),Linux)
	PLATFORMLIBS=-lm -lnsl -lrt -ldl
endif

ifeq ($(shell uname),SunOS)
	PLATFORMLIBS=-lnsl -lrt -lsocket -lresolv
endif

###########################################################################
#
#    The architecture, O/S and build type dependent object repository
#
###########################################################################
repos=$(shell uname -sm | sed 's/\( \{1,\}\)/-/g')-$(B2B)-$(BUILD)

#####################################################################
# 'function' to compute the list of objects given a list of source files
#####################################################################
mkobjs=$(foreach O, $(patsubst %.c, %.co, $(patsubst %.cc, %.cco, $(patsubst %.S, %.So, $($(1)_SRC)))), $(addprefix $(repos)/, $(O)))

#  Define program(s) you'd like to build from the sources.
#     <prog>_SRC  = sourcefiles to be contained in your program
#     <prog>_LIBS = extra libs your progra, might need
#         only set this variable if you actually need it

# etransfer daemon
etd_SRC=src/etd.cc src/reentrant.cc src/etdc_fd.cc src/etdc_etdserver.cc src/etdc_debug.cc src/etd_acl.cc
etd_VERSION=$(shell ./get_version)
etd_RELEASE=prod
etd_OBJS=$(call mkobjs,etd)

# targets that etd depends upon
# Link in support for UDT and SRT
#etd_DEPS=libudt4hv pthread
etd_DEPS=libudt5ab libsrt5ab fkyaml pthread

# etransfer client
etc_SRC=src/etc.cc src/reentrant.cc src/etdc_fd.cc src/etdc_etdserver.cc src/etdc_debug.cc src/etd_acl.cc
etc_VERSION=$(shell ./get_version)
etc_RELEASE=prod
etc_OBJS=$(call mkobjs,etc)

# targets that etc depends upon
# Link in support for UDT and SRT
#etc_DEPS=libudt4hv pthread
etc_DEPS=libudt5ab libsrt5ab pthread


t3_SRC=src/t3.cc
t3_VERSION=3
t3_OBJS=$(call mkobjs,t3)

tsp_SRC=src/tsp.cc 
tsp_VERSION=1
tsp_OBJS=$(call mkobjs,tsp)

t4_SRC=src/t4.cc src/reentrant.cc src/etdc_fd.cc
t4_VERSION=0
t4_OBJS=$(call mkobjs,t4)
t4_DEPS=libudt4hv pthread

tsok_SRC=src/tsok.cc src/reentrant.cc
tsok_VERSION=0
tsok_OBJS=$(call mkobjs,tsok)
tsok_DEPS=libudt4hv pthread

ttls_SRC=src/ttls.cc
ttls_VERSION=0
ttls_OBJS=$(call mkobjs,ttls)
ttls_DEPS=pthread

tACL_SRC=src/tACL.cc src/etd_acl.cc
tACL_VERSION=0
tACL_OBJS=$(call mkobjs,tACL)
tACL_DEPS=fkyaml

# Process make command line targets and filter out the ones that we should build
# This is only to be able to include the correct dependency files
TODO=$(strip $(filter-out install, $(filter-out Repos%, $(filter-out chown, $(filter-out Makefile, $(filter-out clean, $(filter-out info, $(filter-out all, $(MAKECMDGOALS)))))))))
ifeq ($(TODO),)
	TODO=etc etd
endif

# If any of the targets need libudt, add that include path
ifneq ($(strip $(findstring libudt, $(foreach P, $(TODO), $($(P)_DEPS)))),)
	INCD+=-I$(shell pwd)/libudt5ab
	PLATFORMLIBS+=-L./$(repos)/libudt5ab -ludt5ab
	#INCD+=-I$(shell pwd)/libudt4hv
	#PLATFORMLIBS+=-L./$(repos)/libudt4hv -ludt4hv
endif
# If any of the targets need libsrt, add that include path
ifneq ($(strip $(findstring libsrt, $(foreach P, $(TODO), $($(P)_DEPS)))),)
	INCD+=-I$(shell pwd)/libsrt5ab/srtcore
	PLATFORMLIBS+=-L./$(repos)/libsrt5ab -lsrt5ab
endif
# If any of the targets need pthread, add that library
ifneq ($(strip $(findstring pthread, $(foreach P, $(TODO), $($(P)_DEPS)))),)
	BASEOPT+=-pthread -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS 
	PLATFORMLIBS+=-lpthread
endif
# If any of the targets need the YAML parser, add that library
ifneq ($(strip $(findstring fkyaml, $(foreach P, $(TODO), $($(P)_DEPS)))),)
	INCD+=-I$(shell pwd)/fkyaml
endif


# Hints to gmake 
.PHONY: info clean %.depend %.version %.target libudt4hv libudt5ab libsrt5ab pthread %.dep
.PRECIOUS: $(repos)/src/%_version.cco $(repos)/%.d


###################################################################
##                   Targets start here!
###################################################################
all: $(foreach P, $(DEFAULTTARGETS), $(addsuffix .target, $(P)))
	@echo "all: that's all folks!"

info:
	@echo "info: TODO=$(TODO)"; echo "repos=$(repos)"; echo "OBJS: $(foreach T, $(TODO), $($(T)_OBJS))"
	@echo "INCD=$(INCD)"; echo "D=$(D)"; echo "PLATFORMLIBS=$(PLATFORMLIBS)"
	@$(MAKE) -C libudt5ab -f Makefile B2B="$(B2B)" CPP="$(CXX)" REPOS="$(repos)" BUILD="$(BUILD)" info
	@$(MAKE) -C libsrt5ab -f Makefile B2B="$(B2B)" CPP="$(CXX)" REPOS="$(repos)" BUILD="$(BUILD)" info

clean: $(foreach P, $(DEFAULTTARGETS), $(addsuffix .clean, $(P)))
	-$(MAKE) -C libudt4hv -f Makefile B2B="$(B2B)" REPOS="$(repos)" clean
	-$(MAKE) -C libudt5ab -f Makefile B2B="$(B2B)" CPP="$(CXX)" REPOS="$(repos)" BUILD="$(BUILD)" clean
	-$(MAKE) -C libsrt5ab -f Makefile B2B="$(B2B)" CPP="$(CXX)" REPOS="$(repos)" BUILD="$(BUILD)" clean
	@echo "cleaned: $(DEFAULTTARGETS)"

libudt4hv: 
	@$(MAKE) -C libudt4hv -f Makefile B2B="$(B2B)" CPP="$(CXX)" REPOS="$(repos)" BUILD="$(BUILD)"
libudt5ab: 
	@$(MAKE) -C libudt5ab -f Makefile B2B="$(B2B)" CPP="$(CXX)" REPOS="$(repos)" BUILD="$(BUILD)"
libsrt5ab:
	@$(MAKE) -C libsrt5ab -f Makefile B2B="$(B2B)" CPP="$(CXX)" REPOS="$(repos)" BUILD="$(BUILD)"

%.target: %.version %.depend %.dep
	$(LD) -o $* $($*_OBJS) $(repos)/src/$*_version.cco $(LIBD) $(PLATFORMLIBS) $($*F_LIBS)

%.clean:
	-rm -f $($*_OBJS) $(repos)/$* $(repos)/$*.d $(repos)/src/$*_version.cco
	@echo "*.clean: cleaned out [$@]"

#@echo "*.version rule triggered for [$*] {$?}"
%.version: $(repos)/src/%_version.cco

#@echo "*.depend rule triggered for [$*] {$?}"
%.depend: $(repos)/%.d

# Let g++ generate deps for the source files. Then we manually add the
# dependencies listed in the per program specification and also write
# a specific target rule
$(repos)/%.d: Makefile
	@ mkdir -p $(repos)
	@ $(CXX) -MM $(CXXOPT) $(INCD) $($(*F)_SRC) | sed -e 's@^\(.*\)\.o:@$(repos)/src/\1.cco:@;' > $@
	@ export TMP="`cat $@ | sed -n '/^[^:]*:/{ s/^[^:]*: *//;p; }' | tr ' ' '\n' | sort | uniq | tr '\n' ' ' | sed 's#\\\\##g'`"; printf "$(repos)/$*.d $(repos)/src/$*_version.cco: src/version.h $${TMP}\n" >> $@;
	@ printf ".PHONY: $*.dep\n$*.dep : $($*_DEPS)\n" >> $@;
	@ printf "$*.target: $(repos)/src/$*_version.cco $(repos)/$*.d $*.dep $($*_OBJS)\n\t$(LD) -o $(repos)/$* $($*_OBJS) $(repos)/src/$*_version.cco $(LIBD) $(PLATFORMLIBS) $($(*F)_LIBS)\n" >> $@;

$(repos)/src/%_version.cco: 
	@ echo "[creating version file for $* into $@]";
	@ mkdir -p $(repos)
	@ export TMP=`dirname $@`; if [ ! -d "$${TMP}" ]; then mkdir -p "$${TMP}"; fi;
	@ if [ ! -f ".$*.seq" ]; then echo 0 > .$*.seq; fi;
	@ export SEQ=`cat .$*.seq`; sed 's/@@PROG@@/$(*F)/;s/@@PROG_VERSION@@/$($(*F)_VERSION)/;s/@@B2B@@/$(B2B)/;s/@@RELEASE@@/$($(*F)_RELEASE)/;s/@@BUILD@@/$(BUILD)/;s/@@BUILDINFO@@/$(BUILDINFO)/;s/@@DATE@@/$(DATE)/;' src/version.in | $(CXX) -DSEQNR=\"$${SEQ}\" $(CXXOPT) $(INCD) -c -o $@ -pipe -x c++ -; echo `expr $${SEQ} + 1` > .$*.seq

#@echo "[compile] $< into $@"
$(repos)/%.cco: %.cc
	@ mkdir -p $(repos)
	$(CXX) $(CXXOPT) $(INCD) -c -o $@ $<

%: %.target
	@echo "*: generic pattern rule - $@ ($<)"

# if not clean'ing - include dependencies
ifeq ($(findstring clean, $(MAKECMDGOALS)),)
-include $(foreach P, $(TODO), $(addprefix $(repos)/, $(addsuffix .d, $(P))))
endif

