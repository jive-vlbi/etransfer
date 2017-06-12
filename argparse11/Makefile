B2B?=native
OPT?=opt
# on make cmd line add LIBS="-L... -l..." for libs and OBJS="reentrant.cco ..." for objects
LIBS=
OBJS=
CXX?=g++
COMP=$(CXX) -fPIC -g -O2 -Wall -W -Werror -Wextra -pedantic -DB2B=$(B2B) -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 -D_GNU_SOURCE -U_GNU_SOURCE -D__STDC_FORMAT_MACROS -Wcast-qual -Wwrite-strings -Wredundant-decls -Wfloat-equal -Wshadow -D_FILE_OFFSET_BITS=64 -pthread -D_REENTRANT -D_POSIX_PTHREAD_SEMANTICS  -std=c++11 -I.
ifeq ($(B2B),32)
	override COMP   += -m32
endif
ifeq ($(B2B),64)
	override COMP   += -m64
endif
# our "%" catch all conflicts with built-in implicit rule
# thanks @ http://stackoverflow.com/a/4126617
.SUFFIXES:

###########################################################################
#
#    The architecture, O/S and build type dependent object repository
#
###########################################################################
repos=$(shell uname -sm | sed 's/\( \{1,\}\)/-/g')-$(B2B)-$(OPT)
.PHONY: %.pp %.clean %.S %
.PRECIOUS: $(repos)/%

$(repos)/%: %.cc
	-mkdir -p $(repos)
	$(COMP)  -o $(repos)/$* $(addprefix ../$(repos)/src/, $(OBJS)) $(LIBS) $<

%.pp: %.cc
	$(COMP) -E $<

%.S: %.cc
	$(COMP) -S -o $(repos)/$@ $<

%.clean:
	-rm $(repos)/$*
	-rmdir $(repos)

%: $(repos)/%
	@echo "Yup, all's up to date"
