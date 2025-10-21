# Compiler and flags for C++
CXX = g++
CXXFLAGS = -fPIC -Wall -Wextra -O2 -std=c++11
LDFLAGS = -shared -pthread

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
