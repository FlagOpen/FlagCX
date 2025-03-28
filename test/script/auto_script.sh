#!/bin/bash
BUILD_DIR="build"

mkdir -p $BUILD_DIR


make USE_NVIDIA=1

if [ $? -ne 0 ]; then
    echo "Compilation failed!"
    exit 1
fi

cd test/perf

make USE_NVIDIA=1

if [ $? -ne 0 ]; then
    echo "Test compilation failed!"
    exit 1
fi

mpirun -np 8 ./test_allreduce -b 128M -e 8G -f 2

if [ $? -ne 0 ]; then
    echo "Test execution failed!"
    exit 1
fi

