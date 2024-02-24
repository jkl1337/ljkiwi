-include config.mk

CC := $(CROSS)gcc
CP := cp
RM := rm
LIBFLAG := -shared
LIB_EXT := so
LUA_INCDIR := /usr/include

SRCDIR := .

OPTFLAG := -O2
CCFLAGS += $(OPTFLAG) -fPIC -Wall -fvisibility=hidden -Wformat=2 -Wconversion -Wimplicit-fallthrough

SANITIZE_FLAGS := -fstrict-flex-arrays -fsanitize=undefined -fsanitize=address
LTO_FLAGS := -flto=auto

ifdef SANITIZE
	CCFLAGS += $(SANITIZE_FLAGS)
endif
ifdef LTO
	CCFLAGS += $(LTO_FLAGS)
endif

override CPPFLAGS += -I$(SRCDIR) -I$(SRCDIR)/kiwi -I$(LUA_INCDIR)
override CXXFLAGS += -std=c++14 -fno-rtti $(CCFLAGS)
override CFLAGS += -std=c99 $(CCFLAGS)

ifneq ($(filter %gcc,$(CC)),)
  CXX := $(patsubst %gcc,%g++,$(CC))
  PCH := ljkiwi.hpp.gch
else
  ifneq ($(filter %clang,$(CC)),)
    CXX := $(patsubst %clang,%clang++,$(CC))
    override CXXFLAGS += -pedantic -Wno-c99-extensions
		PCH := ljkiwi.hpp.pch
endif
endif

ifdef LUA
LUA_VERSION ?= $(lastword $(shell $(LUA) -e "print(_VERSION)"))
endif

ifndef LUA_VERSION
LJKIWI_CKIWI := 1
else
  ifeq ($(LUA_VERSION),5.1)
    LJKIWI_CKIWI := 1
  endif
endif

KIWI_LIB := AssocVector.h constraint.h debug.h errors.h expression.h kiwi.h maptype.h \
  row.h shareddata.h solver.h solverimpl.h strength.h symbol.h symbolics.h term.h \
  util.h variable.h version.h

OBJS := luakiwi.o
ifdef LJKIWI_CKIWI
  OBJS += ckiwi.o
endif

vpath %.cpp $(SRCDIR)/ckiwi $(SRCDIR)/luakiwi
vpath %.h $(SRCDIR)/ckiwi $(SRCDIR)/luakiwi $(SRCDIR)/kiwi/kiwi

all: ljkiwi.$(LIB_EXT)

install:
	$(CP) -f ljkiwi.$(LIB_EXT) $(INST_LIBDIR)/ljkiwi.$(LIB_EXT)
	$(CP) -f kiwi.lua $(INST_LUADIR)/kiwi.lua

clean:
	$(RM) -f ljkiwi.$(LIB_EXT) $(OBJS) $(PCH)


ljkiwi.hpp.gch: $(KIWI_LIB)
ckiwi.o: $(PCH) ckiwi.cpp ckiwi.h $(KIWI_LIB)
luakiwi.o: $(PCH) luakiwi-int.h luacompat.h $(KIWI_LIB)

ljkiwi.$(LIB_EXT): $(OBJS)
	$(CXX) $(CCFLAGS) $(LIBFLAG) -o $@ $(OBJS)

%.hpp.gch: %.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -x c++-header -o $@ $<

%.hpp.pch: %.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -x c++-header -o $@ $<

%.o: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

.PHONY: all install clean
