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

CMD='torchrun --nproc_per_node 8 --nnodes=1 --node_rank=0 --master_addr="172.24.135.58" --master_port=8281 example.py'

echo $CMD
eval $CMD
