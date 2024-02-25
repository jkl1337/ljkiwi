CXX := $(CROSS)g++
CP := cp
RM := rm
LIBFLAG := -shared
LIB_EXT := $(if $(filter Windows_NT,$(OS)),dll,so)
LUA_INCDIR := /usr/include

SRCDIR := .

IS_CLANG = $(filter %clang++,$(CXX))
IS_GCC = $(filter %g++,$(CXX))

OPTFLAG := -O2
SANITIZE_FLAGS := -fsanitize=undefined -fsanitize=address -fsanitize=alignment -fsanitize=bounds-strict \
 -fsanitize=shift -fsanitize=unreachable -fsanitize=bool \
 -fsanitize=enum

LTO_FLAGS := -flto=auto

-include config.mk

ifeq ($(origin LUAROCKS), command line)
	CCFLAGS := $(CFLAGS)
	override CFLAGS := -std=c99 $(CCFLAGS)

	ifneq ($(filter %gcc,$(CC)),)
		CXX := $(patsubst %gcc,%g++,$(CC))
	else
	ifneq ($(filter %clang,$(CC)),)
		CXX := $(patsubst %clang,%clang++,$(CC))
	endif
	endif

luarocks: mostlyclean ljkiwi.$(LIB_EXT)

else
	CCFLAGS += -fPIC $(OPTFLAG)
	override CFLAGS += -std=c99 $(CCFLAGS)
endif

CCFLAGS += -Wall -fvisibility=hidden -Wformat=2 -Wconversion -Wimplicit-fallthrough

ifdef FSANITIZE
	CCFLAGS += $(SANITIZE_FLAGS)
endif
ifndef FNOLTO
	CCFLAGS += $(LTO_FLAGS)
endif

ifneq ($(IS_GCC),)
	PCH := ljkiwi.hpp.gch
else
ifneq ($(IS_CLANG),)
	override CXXFLAGS += -pedantic -Wno-c99-extensions
	PCH := ljkiwi.hpp.pch
endif
endif

override CPPFLAGS += -I$(SRCDIR) -I$(SRCDIR)/kiwi -I"$(LUA_INCDIR)"
override CXXFLAGS += -std=c++14 -fno-rtti $(CCFLAGS)

ifeq ($(OS),Windows_NT)
override CPPFLAGS += -DLUA_BUILD_AS_DLL
override LIBFLAG += "$(LUA_LIBDIR)/$(LUALIB)"
endif

ifdef LUA
LUA_VERSION ?= $(lastword $(shell "$(LUA)" -e "print(_VERSION)"))
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

mostlyclean:
	$(RM) -f ljkiwi.$(LIB_EXT) $(OBJS)

clean: mostlyclean
	$(RM) -f $(PCH)

ckiwi.o: $(PCH) ckiwi.cpp ckiwi.h $(KIWI_LIB)
luakiwi.o: $(PCH) luakiwi-int.h luacompat.h $(KIWI_LIB)
$(PCH): $(KIWI_LIB)

ljkiwi.$(LIB_EXT): $(OBJS)
	$(CXX) $(CCFLAGS) $(LIBFLAG) -o $@ $(OBJS)

%.hpp.gch: %.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -x c++-header -o $@ $<

%.hpp.pch: %.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -x c++-header -o $@ $<

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

.PHONY: all install clean mostlyclean
