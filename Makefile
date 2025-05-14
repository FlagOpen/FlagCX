# 2025 - Modified by MetaX Integrated Circuits (Shanghai) Co., Ltd. All Rights Reserved.
# 2025 - Modified by DU. All Rights Reserved.
BUILDDIR ?= $(abspath ./build)

# set to 0 if not provided
USE_NVIDIA ?= 0
USE_ILUVATAR_COREX ?= 0
USE_CAMBRICON ?= 0
USE_GLOO ?= 0
USE_BOOTSTRAP ?= 0
USE_METAX ?= 0
USE_DU ?= 0

# set to empty if not provided
DEVICE_HOME ?=
CCL_HOME ?=
HOST_CCL_HOME ?=

ifeq ($(strip $(DEVICE_HOME)),)
	ifeq ($(USE_NVIDIA), 1)
		DEVICE_HOME = /usr/local/cuda
	else ifeq ($(USE_ILUVATAR_COREX), 1)
		DEVICE_HOME = /usr/local/corex
	else ifeq ($(USE_CAMBRICON), 1)
		DEVICE_HOME = $(NEUWARE_HOME)
	else ifeq ($(USE_METAX), 1)
		DEVICE_HOME = /opt/maca
	else ifeq ($(USE_DU), 1)
		DEVICE_HOME = ${CUDA_PATH}
	else
		DEVICE_HOME = /usr/local/cuda
	endif
endif

ifeq ($(strip $(CCL_HOME)),)
	ifeq ($(USE_NVIDIA), 1)
		CCL_HOME = /usr/local/nccl/build
	else ifeq ($(USE_ILUVATAR_COREX), 1)
		CCL_HOME = /usr/local/corex
	else ifeq ($(USE_CAMBRICON), 1)
		CCL_HOME = $(NEUWARE_HOME)
	else ifeq ($(USE_METAX), 1)
		CCL_HOME = /opt/maca
	else ifeq ($(USE_DU), 1)
		CCL_HOME = ${CUDA_PATH}
	else
		CCL_HOME = /usr/local/nccl/build
	endif
endif

ifeq ($(strip $(HOST_CCL_HOME)),)
	ifeq ($(USE_GLOO), 1)
		HOST_CCL_HOME = /usr/local
	else
		HOST_CCL_HOME = 
	endif
endif

DEVICE_LIB =
DEVICE_INCLUDE =
DEVICE_LINK =
CCL_LIB =
CCL_INCLUDE =
CCL_LINK =
HOST_CCL_LIB = 
HOST_CCL_INCLUDE =
HOST_CCL_LINK =
ADAPTOR_FLAG =
HOST_CCL_ADAPTOR_FLAG =
ifeq ($(USE_NVIDIA), 1)
	DEVICE_LIB = $(DEVICE_HOME)/lib64
	DEVICE_INCLUDE = $(DEVICE_HOME)/include
	DEVICE_LINK = -lcudart -lcuda
	CCL_LIB = $(CCL_HOME)/lib
	CCL_INCLUDE = $(CCL_HOME)/include
	CCL_LINK = -lnccl
	ADAPTOR_FLAG = -DUSE_NVIDIA_ADAPTOR
else ifeq ($(USE_ILUVATAR_COREX), 1)
	DEVICE_LIB = $(DEVICE_HOME)/lib
	DEVICE_INCLUDE = $(DEVICE_HOME)/include
	DEVICE_LINK = -lcudart -lcuda
	CCL_LIB = $(CCL_HOME)/lib
	CCL_INCLUDE = $(CCL_HOME)/include
	CCL_LINK = -lnccl
	ADAPTOR_FLAG = -DUSE_ILUVATAR_COREX_ADAPTOR
else ifeq ($(USE_CAMBRICON), 1)
	DEVICE_LIB = $(DEVICE_HOME)/lib64
	DEVICE_INCLUDE = $(DEVICE_HOME)/include
	DEVICE_LINK = -lcnrt
	CCL_LIB = $(CCL_HOME)/lib64
	CCL_INCLUDE = $(CCL_HOME)/include
	CCL_LINK = -lcncl
	ADAPTOR_FLAG = -DUSE_CAMBRICON_ADAPTOR
else ifeq ($(USE_METAX), 1)
	DEVICE_LIB = $(DEVICE_HOME)/lib64
	DEVICE_INCLUDE = $(DEVICE_HOME)/include
	CCL_LIB = $(CCL_HOME)/lib64
	CCL_INCLUDE = $(CCL_HOME)/include
	CCL_LINK = -lmccl
	ADAPTOR_FLAG = -DUSE_METAX_ADAPTOR
else ifeq ($(USE_DU), 1)
	DEVICE_LIB = $(DEVICE_HOME)/lib64
	DEVICE_INCLUDE = $(DEVICE_HOME)/include
	DEVICE_LINK = -lcudart -lcuda
	CCL_LIB = $(CCL_HOME)/lib64
	CCL_INCLUDE = $(CCL_HOME)/include
	CCL_LINK = -lnccl
	ADAPTOR_FLAG = -DUSE_DU_ADAPTOR
else
	DEVICE_LIB = $(DEVICE_HOME)/lib64
	DEVICE_INCLUDE = $(DEVICE_HOME)/include
	DEVICE_LINK = -lcudart -lcuda
	CCL_LIB = $(CCL_HOME)/lib
	CCL_INCLUDE = $(CCL_HOME)/include
	CCL_LINK = -lnccl
	ADAPTOR_FLAG = -DUSE_NVIDIA_ADAPTOR
endif

ifeq ($(USE_GLOO), 1)
	HOST_CCL_LIB = $(HOST_CCL_HOME)/lib
	HOST_CCL_INCLUDE = $(HOST_CCL_HOME)/include
	HOST_CCL_LINK = -lgloo
	HOST_CCL_ADAPTOR_FLAG = -DUSE_GLOO_ADAPTOR
else ifeq ($(USE_BOOTSTRAP), 1)
	HOST_CCL_LIB = /usr/local/lib
	HOST_CCL_INCLUDE = /usr/local/include
	HOST_CCL_LINK = 
	HOST_CCL_ADAPTOR_FLAG = -DUSE_BOOTSTRAP_ADAPTOR
else
	HOST_CCL_LIB = /usr/local/lib
	HOST_CCL_INCLUDE = /usr/local/include
	HOST_CCL_LINK = 
	HOST_CCL_ADAPTOR_FLAG = -DUSE_BOOTSTRAP_ADAPTOR
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
	@echo "DEVICE_HOME: $(DEVICE_HOME)"
	@echo "CCL_HOME: $(CCL_HOME)"
	@echo "HOST_CCL_HOME: $(HOST_CCL_HOME)"
	@echo "USE_NVIDIA: $(USE_NVIDIA)"
	@echo "USE_ILUVATAR_COREX: $(USE_ILUVATAR_COREX)"
	@echo "USE_CAMBRICON: $(USE_CAMBRICON)"
	@echo "USE_GLOO: $(USE_GLOO)"
	@echo "USE_DU: $(USE_DU)"
	@echo "DEVICE_LIB: $(DEVICE_LIB)"
	@echo "DEVICE_INCLUDE: $(DEVICE_INCLUDE)"
	@echo "CCL_LIB: $(CCL_LIB)"
	@echo "CCL_INCLUDE: $(CCL_INCLUDE)"
	@echo "HOST_CCL_LIB: $(HOST_CCL_LIB)"
	@echo "HOST_CCL_INCLUDE: $(HOST_CCL_INCLUDE)"
	@echo "ADAPTOR_FLAG: $(ADAPTOR_FLAG)"
	@echo "HOST_CCL_ADAPTOR_FLAG: $(HOST_CCL_ADAPTOR_FLAG)"

$(LIBDIR)/$(TARGET): $(LIBOBJ)
	@mkdir -p `dirname $@`
	@echo "Linking   $@"
	@g++ $(LIBOBJ) -o $@ -L$(CCL_LIB) -L$(DEVICE_LIB) -L$(HOST_CCL_LIB) -shared -fvisibility=default -Wl,--no-as-needed -Wl,-rpath,$(LIBDIR) -Wl,-rpath,$(CCL_LIB) -Wl,-rpath,$(HOST_CCL_LIB) -lpthread -lrt -ldl $(CCL_LINK) $(DEVICE_LINK) $(HOST_CCL_LINK) -g

$(OBJDIR)/%.o: %.cc
	@mkdir -p `dirname $@`
	@echo "Compiling $@"
	@g++ $< -o $@ $(foreach dir,$(INCLUDEDIR),-I$(dir)) -I$(CCL_INCLUDE) -I$(DEVICE_INCLUDE) -I$(HOST_CCL_INCLUDE) $(ADAPTOR_FLAG) $(HOST_CCL_ADAPTOR_FLAG) -c -fPIC -fvisibility=default -Wvla -Wno-unused-function -Wno-sign-compare -Wall -MMD -MP -g

-include $(LIBOBJ:.o=.d)

clean:
	@rm -rf $(LIBDIR)/$(TARGET) $(OBJDIR)
