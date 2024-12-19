BUILDDIR ?= $(abspath ./build)
USE_NVIDIA ?= 0
USE_ILUVATAR_COREX ?= 0
USE_CAMBRICON ?= 0

DEVICE_LIB =
DEVICE_INCLUDE =
DEVICE_LINK =
CCL_LIB =
CCL_INCLUDE =
CCL_LINK =
ADAPTOR_FLAG =
ifeq ($(USE_NVIDIA), 1)
	DEVICE_LIB = /usr/local/cuda/lib64
	DEVICE_INCLUDE = /usr/local/cuda/include
	DEVICE_LINK = -lcudart -lcuda
	CCL_LIB = /usr/local/nccl/build/lib
	CCL_INCLUDE = /usr/local/nccl/build/include
	CCL_LINK = -lnccl
	ADAPTOR_FLAG = -DUSE_NVIDIA_ADAPTOR
else ifeq ($(USE_ILUVATAR_COREX), 1)
	DEVICE_LIB = /usr/local/corex/lib
	DEVICE_INCLUDE = /usr/local/corex/include
	DEVICE_LINK = -lcudart -lcuda
	CCL_LIB = /usr/local/corex/lib
	CCL_INCLUDE = /usr/local/corex/include
	CCL_LINK = -lnccl
	ADAPTOR_FLAG = -DUSE_ILUVATAR_COREX_ADAPTOR
else ifeq ($(USE_CAMBRICON), 1)
	DEVICE_LIB = /torch/neuware_home/lib64
	DEVICE_INCLUDE = /torch/neuware_home/include
	DEVICE_LINK = -lcnrt
	CCL_LIB = /torch/neuware_home/lib64
	CCL_INCLUDE = /torch/neuware_home/include
	CCL_LINK = -lcncl
	ADAPTOR_FLAG = -DUSE_CAMBRICON_ADAPTOR
else
	DEVICE_LIB = /usr/local/cuda/lib64
	DEVICE_INCLUDE = /usr/local/cuda/include
	DEVICE_LINK = -lcudart -lcuda
	CCL_LIB = /usr/local/nccl/build/lib
	CCL_INCLUDE = /usr/local/nccl/build/include/
	CCL_LINK = -lnccl
	ADAPTOR_FLAG = -DUSE_NVIDIA_ADAPTOR
endif

LIBDIR := $(BUILDDIR)/lib
OBJDIR := $(BUILDDIR)/obj

INCLUDEDIR := \
	$(abspath flagcx/include) \
	$(abspath flagcx/core) \
	$(abspath flagcx/adaptor) \
	$(abspath flagcx/service)

LIBSRCFILES:= \
	$(wildcard flagcx/*.cc) \
	$(wildcard flagcx/core/*.cc) \
	$(wildcard flagcx/adaptor/*.cc) \
	$(wildcard flagcx/service/*.cc)

LIBOBJ     := $(LIBSRCFILES:%.cc=$(OBJDIR)/%.o)

TARGET = libflagcx.so
all: $(LIBDIR)/$(TARGET)

print_var:
	@echo "USE_NVIDIA: $(USE_NVIDIA)"
	@echo "USE_ILUVATAR_COREX: $(USE_ILUVATAR_COREX)"
	@echo "USE_CAMBRICON: $(USE_CAMBRICON)"
	@echo "DEVICE_LIB: $(DEVICE_LIB)"
	@echo "DEVICE_INCLUDE: $(DEVICE_INCLUDE)"
	@echo "CCL_LIB: $(CCL_LIB)"
	@echo "CCL_INCLUDE: $(CCL_INCLUDE)"
	@echo "ADAPTOR_FLAG: $(ADAPTOR_FLAG)"

$(LIBDIR)/$(TARGET): $(LIBOBJ)
	@mkdir -p `dirname $@`
	@echo "Linking   $@"
	@g++ $(LIBOBJ) -o $@ -L$(CCL_LIB) -L$(DEVICE_LIB) -shared -fvisibility=default -Wl,--no-as-needed -Wl,-rpath,$(LIBDIR) -Wl,-rpath,$(CCL_LIB) -lpthread -lrt -ldl $(CCL_LINK) $(DEVICE_LINK) -g

$(OBJDIR)/%.o: %.cc
	@mkdir -p `dirname $@`
	@echo "Compiling $@"
	@g++ $< -o $@ $(foreach dir,$(INCLUDEDIR),-I$(dir)) -I$(CCL_INCLUDE) -I$(DEVICE_INCLUDE) $(ADAPTOR_FLAG) -c -fPIC -fvisibility=default -Wvla -Wno-unused-function -Wno-sign-compare -Wall -MMD -MP -g

-include $(LIBOBJ:.o=.d)

clean:
	@rm -rf $(LIBDIR)/$(TARGET) $(OBJDIR)
