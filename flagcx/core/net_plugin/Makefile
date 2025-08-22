# Makefile for net_ucx_uct plugin
CXX = g++
CXXFLAGS = -std=c++11 -fPIC -Wall -Wextra -O2 -g
CXXFLAGS += -Wno-unused-parameter -Wno-missing-field-initializers -Wno-unused-function -Wno-sign-compare
CXXFLAGS += -DUSE_UCX=1 -DHAVE_UCX_PLUGIN=1 -DHAVE_IB_PLUGIN=0


SRCDIR = .
BUILDDIR = ./build
LIBDIR = .

SOURCES = net_ucx_uct.cc ucx_uct_lib.cc
OBJECTS = $(SOURCES:%.cc=$(BUILDDIR)/%.o)

TARGET = libflagcx-net-ucx.so

INCLUDES = -I. \
           -I../../include \
           -I../../core \
           -I../../service \
           -I../../../flagcx/include \
           -I../../../flagcx/core

UCX_HOME ?= /usr/local/ucx
UCX_INCLUDES = -I$(UCX_HOME)/include
UCX_LIBS = -L$(UCX_HOME)/lib -lucp -lucs -luct

SYSTEM_LIBS = -lpthread -lrt -ldl

LIBS = $(UCX_LIBS) $(SYSTEM_LIBS)

all: $(LIBDIR)/$(TARGET)

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(LIBDIR):
	mkdir -p $(LIBDIR)

$(BUILDDIR)/%.o: %.cc | $(BUILDDIR)
	@echo "Compiling $<"
	@$(CXX) $(CXXFLAGS) $(INCLUDES) $(UCX_INCLUDES) -c $< -o $@

$(LIBDIR)/$(TARGET): $(OBJECTS) | $(LIBDIR)
	@echo "Linking $@"
	@$(CXX) -shared -o $@ $(OBJECTS) $(LIBS)
	@echo "Library created: $@"

install: $(LIBDIR)/$(TARGET)
	@echo "Installing $(TARGET) to /usr/local/lib"
	@sudo cp $(LIBDIR)/$(TARGET) /usr/local/lib/
	@sudo ldconfig

clean:
	@echo "Cleaning build files"
	@rm -rf $(BUILDDIR)
	@rm -f $(LIBDIR)/$(TARGET)

print_vars:
	@echo "CXX = $(CXX)"
	@echo "CXXFLAGS = $(CXXFLAGS)"
	@echo "UCX_HOME = $(UCX_HOME)"
	@echo "SOURCES = $(SOURCES)"
	@echo "OBJECTS = $(OBJECTS)"
	@echo "TARGET = $(TARGET)"
	@echo "INCLUDES = $(INCLUDES)"
	@echo "LIBS = $(LIBS)"

help:
	@echo "Available targets:"
	@echo "  all        - Build the library (default)"
	@echo "  clean      - Remove build files"
	@echo "  install    - Install library to system"
	@echo "  print_vars - Show build variables"
	@echo "  help       - Show this help message"
	@echo ""
	@echo "Environment variables:"
	@echo "  UCX_HOME   - Path to UCX installation (default: /usr/local/ucx)"
	@echo "  CXX        - C++ compiler (default: g++)"

.PHONY: all clean install print_vars help
