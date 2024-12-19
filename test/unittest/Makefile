BUILDDIR ?= $(abspath ./build)

BINDIR := $(BUILDDIR)/bin
OBJDIR := $(BUILDDIR)/obj

INCLUDEDIR := \
	$(abspath include) \
	$(abspath coll/include)

LIBSRCFILES:= \
	$(wildcard *.cpp) \
	$(wildcard coll/*.cpp)

BINOBJ := $(LIBSRCFILES:%.cpp=$(OBJDIR)/%.o)

TARGET = main
all: $(BINDIR)/$(TARGET)

$(BINDIR)/$(TARGET): $(BINOBJ)
	@mkdir -p `dirname $@`
	@echo "Linking   $@"
	@g++ $(BINOBJ) -o $@ -L../../build/lib -L/usr/local/mpi/lib -L/usr/local/lib -Wl,--no-as-needed -Wl,-rpath,../../build/lib -lflagcx -lmpi -lgtest -g

$(OBJDIR)/%.o: %.cpp
	@mkdir -p `dirname $@`
	@echo "Compiling $@"
	@g++ $< -o $@ $(foreach dir,$(INCLUDEDIR),-I$(dir)) -I../../flagcx/include -I/usr/local/mpi/include -I/usr/local/include -c -Wvla -Wno-unused-function -Wno-sign-compare -Wall -g

-include $(BINOBJ:.o=.d)

clean:
	@rm -rf $(BINDIR)/$(TARGET) $(OBJDIR)

run-allreduce:
	@mpirun --allow-run-as-root -np 8 -x UCX_POSIX_USE_PROC_LINK=n -x CUDA_VISIBLE_DEVICES=0,1,2,3,4,5,6,7 -x FLAGCX_LOG_LEVEL=INFO -x FLAGCX_LOG_SUBSYS=ALL ./build/bin/main
