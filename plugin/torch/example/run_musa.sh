#!/bin/bash

# Check if the debug flag is provided as an argument
if [ "$1" == "debug" ]; then
    export MCCL_DEBUG=INFO
    export MCCL_DEBUG_SUBSYS=all
    echo "MCCL debug information enabled."
else
    unset MCCL_DEBUG
    unset MCCL_DEBUG_SUBSYS
    echo "MCCL debug information disabled."
fi

export FLAGCX_DEBUG=INFO
export FLAGCX_DEBUG_SUBSYS=ALL
export MUSA_VISIBLE_DEVICES=0,1,2,3,4,5,6,7

export TORCH_DISTRIBUTED_DETAIL=DEBUG
CMD='torchrun --nproc_per_node 8 --nnodes=1 --node_rank=0 --master_addr="localhost" --master_port=8281 example_musa.py'

echo $CMD
eval $CMD
