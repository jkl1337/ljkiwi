CXX := $(CROSS)g++
CP := cp
RM := rm
LIBFLAG := -shared
LIB_EXT := $(if $(filter Windows_NT,$(OS)),dll,so)
LUA_INCDIR := /usr/include

SRCDIR := .

SANITIZE_FLAGS := -fsanitize=undefined -fsanitize=address -fsanitize=alignment \
  -fsanitize=shift -fsanitize=unreachable -fsanitize=bool -fsanitize=enum

ifdef FDEBUG
  OPTFLAG := -O2
else
  OPTFLAG := -Og -g
endif

COVERAGE_FLAGS := --coverage
LTO_FLAGS := -flto=auto

ifeq ($(OS),Windows_NT)
  is_clang = $(filter %clang++,$(CXX))
  is_gcc = $(filter %g++,$(CXX))

  ifdef FSANITIZE
    $(error "FSANITIZE is not supported on Windows")
  endif
else
  uname_s := $(shell uname -s)
  ifeq ($(uname_s),Darwin)
    is_clang = 1
    is_gcc =

    CC := env MACOSX_DEPLOYMENT_TARGET=11.0 gcc
    CXX := env MACOSX_DEPLOYMENT_TARGET=11.0 g++
    LIBFLAG := -bundle -undefined dynamic_lookup

  else
    is_clang = $(filter %clang++,$(CXX))
    is_gcc = $(filter %g++,$(CXX))

    SANITIZE_FLAGS += -fsanitize=bounds-strict
  endif
endif

-include config.mk

ifeq ($(origin LUAROCKS), command line)
  ifdef FCOV
    CCFLAGS := $(patsubst -O%,,$(CFLAGS))
  else
    ifdef FDEBUG
      CCFLAGS := $(patsubst -O%,,$(CFLAGS)) -Og -g
    else
      CCFLAGS := $(CFLAGS)
    endif
  endif
  override CFLAGS := -std=c99 $(CCFLAGS)

  ifneq ($(filter %gcc,$(CC)),)
    CXX := $(patsubst %gcc,%g++,$(CC))
  else
    ifneq ($(filter %clang,$(CC)),)
      CXX := $(patsubst %clang,%clang++,$(CC))
    endif
  endif

else
  CCFLAGS += -fPIC $(OPTFLAG)
  override CFLAGS += -std=c99 $(CCFLAGS)
endif

CCFLAGS += -Wall -fvisibility=hidden -Wformat=2 -Wconversion -Wimplicit-fallthrough

ifdef FCOV
  CCFLAGS += $(COVERAGE_FLAGS)
endif
ifdef FSANITIZE
  CCFLAGS += $(SANITIZE_FLAGS)
endif
ifdef FLTO
  CCFLAGS += $(LTO_FLAGS)
endif

ifneq ($(is_gcc),)
  #PCH := ljkiwi.hpp.gch
else
  ifneq ($(is_clang),)
    override CXXFLAGS += -pedantic -Wno-c99-extensions
    #PCH := ljkiwi.hpp.pch
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

ifdef LUA_VERSION
ifneq ($(LUA_VERSION),5.1)
  LJKIWI_CFFI ?= 0
endif
endif

kiwi_lib_srcs := AssocVector.h constraint.h debug.h errors.h expression.h kiwi.h maptype.h \
  row.h shareddata.h solver.h solverimpl.h strength.h symbol.h symbolics.h term.h \
  util.h variable.h version.h

ifneq ($(LJKIWI_LUA),0)
  objs += luakiwi.o
endif
ifneq ($(LJKIWI_CFFI),0)
  objs += ckiwi.o
endif

vpath %.cpp $(SRCDIR)/ckiwi $(SRCDIR)/luakiwi
vpath %.h $(SRCDIR)/ckiwi $(SRCDIR)/luakiwi $(SRCDIR)/kiwi/kiwi

all: ljkiwi.$(LIB_EXT)

install:
	$(CP) -f ljkiwi.$(LIB_EXT) $(INST_LIBDIR)/ljkiwi.$(LIB_EXT)
	$(CP) -f kiwi.lua $(INST_LUADIR)/kiwi.lua

mostlyclean:
	$(RM) -f ljkiwi.$(LIB_EXT) $(objs) $(objs:.o=.gcda) $(objs:.o=.gcno)

clean: mostlyclean
	$(RM) -f $(PCH)

ckiwi.o: $(PCH) ckiwi.cpp ckiwi.h $(kiwi_lib_srcs)
luakiwi.o: $(PCH) luakiwi-int.h luacompat.h $(kiwi_lib_srcs)
$(PCH): $(kiwi_lib_srcs)

ljkiwi.$(LIB_EXT): $(objs)
	$(CXX) $(CCFLAGS) $(LIBFLAG) -o $@ $(objs)

%.hpp.gch: %.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -x c++-header -o $@ $<

%.hpp.pch: %.hpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -o $@ -x c++-header $<

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

.PHONY: all install clean mostlyclean
