#!/bin/bash

# Check if the debug flag is provided as an argument
if [ "$1" == "debug" ]; then
    export NCCL_DEBUG=INFO
    export NCCL_DEBUG_SUBSYS=all
    echo "NCCL debug information enabled."
else
    unset NCCL_DEBUG
    unset NCCL_DEBUG_SUBSYS
    echo "NCCL debug information disabled."
fi

export FLAGCX_DEBUG=INFO
export FLAGCX_DEBUG_SUBSYS=ALL
export CUDA_VISIBLE_DEVICES=0,1,2,3,4,5,6,7
# Need to preload customized gloo library specified for FlagCX linkage
export LD_PRELOAD=/opt/xre/so/libcuda.so.1:/opt/xre/so/libcuda.so:/opt/xre/so/libcudart.so:/opt/xre/so/libxpuml.so.1:/opt/xre/so/libxpurt.so.2:/opt/xccl/so/libbkcl.so
export LD_LIBRARY_PATH=/opt/xre/so:/opt/xccl/so:$LD_LIBRARY_PATH
export TORCH_DISTRIBUTED_DETAIL=DEBUG
export CUDART_DUMMY_REGISTER=1
export XPU_FORCE_SHARED_DEVICE_CONTEXT=1
CMD='torchrun --nproc_per_node 8 --nnodes=1 --node_rank=0 --master_addr="localhost" --master_port=8281 example.py -o allreduce'

echo $CMD
eval $CMD
