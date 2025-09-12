#!/bin/bash

export CUDA_VISIBLE_DEVICES=0,1,2,3,4,5,6,7
export FLAGCX_DEBUG=INFO
export FLAGCX_DEBUG_SUBSYS=INIT

echo "[INFO] Launching PyTorch API tests in homogeneous mode"
CMD='torchrun --nproc_per_node 8 --nnodes=1 --node_rank=0 --master_addr="localhost" --master_port=8281 ../../plugin/torch/example/example.py'
echo $CMD
eval $CMD
echo "[INFO] Completed PyTorch API tests in homogeneous mode"
echo "--------------------------------------------------------"

echo "[INFO] Launching PyTorch API tests in heterogeneous mode"
export FLAGCX_CLUSTER_SPLIT_LIST=2
CMD='torchrun --nproc_per_node 8 --nnodes=1 --node_rank=0 --master_addr="localhost" --master_port=8281 ../../plugin/torch/example/example.py'
echo $CMD
eval $CMD
echo "[INFO] Completed PyTorch API tests in heterogeneous mode"
echo "--------------------------------------------------------"