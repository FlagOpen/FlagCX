# Compiler and flags for C++
CXX = g++
CXXFLAGS = -fPIC -Wall -Wextra -O2 -std=c++11
LDFLAGS = -shared -pthread

# set to 0 if not provided
USE_NVIDIA ?= 0
USE_ASCEND ?= 0
USE_ILUVATAR_COREX ?= 0
USE_CAMBRICON ?= 0
USE_GLOO ?= 0
USE_BOOTSTRAP ?= 0
USE_METAX ?= 0
USE_MUSA ?= 0
USE_KUNLUNXIN ?=0
USE_AMD ?= 0
USE_DU ?= 0
USE_MPI ?= 0
USE_UCX ?= 0
USE_IBUC ?= 0

ifeq ($(USE_NVIDIA), 1)
	CXXFLAGS += -DUSE_NVIDIA_ADAPTOR
else ifeq ($(USE_ASCEND), 1)
	CXXFLAGS += -DUSE_ASCEND_ADAPTOR
else ifeq ($(USE_ILUVATAR_COREX), 1)
	CXXFLAGS = -DUSE_ILUVATAR_COREX_ADAPTOR
else ifeq ($(USE_CAMBRICON), 1)
	CXXFLAGS = -DUSE_CAMBRICON_ADAPTOR
else ifeq ($(USE_METAX), 1)
	CXXFLAGS = -DUSE_METAX_ADAPTOR
else ifeq ($(USE_MUSA), 1)
	CXXFLAGS = -DUSE_MUSA_ADAPTOR
else ifeq ($(USE_KUNLUNXIN), 1)
	CXXFLAGS = -DUSE_KUNLUNXIN_ADAPTOR
else ifeq ($(USE_DU), 1)
	CXXFLAGS = -DUSE_DU_ADAPTOR
else ifeq ($(USE_AMD), 1)
	CXXFLAGS = -DUSE_AMD_ADAPTOR -D__HIP_PLATFORM_AMD__
else
	CXXFLAGS = -DUSE_NVIDIA_ADAPTOR
endif

# Target shared library name
TARGET = libnccl_param.so

# Source files
SOURCES = nccl_param.cc

# Object files (automatically generated from source files)
OBJECTS = $(SOURCES:.cc=.o)

# Default target
all: $(TARGET)

# Build the shared library
$(TARGET): $(OBJECTS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJECTS)

# Compile C++ source files to object files
%.o: %.cc
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Clean up build files
clean:
	rm -f $(OBJECTS) $(TARGET)

# Phony targets
.PHONY: all clean
