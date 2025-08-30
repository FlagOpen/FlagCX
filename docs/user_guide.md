## Environment Configuration

Refer to the environment setup section in [getting_started.md](https://github.com/whollo/FlagCX/blob/add-flagcx-wuh/docs/getting_started.md)

## Installation and Compilation

Refer to [getting_started.md](https://github.com/whollo/FlagCX/blob/add-flagcx-wuh/docs/getting_started.md) for FlagCX compilation and installation

## Homogeneous Tests Using FlagCX

### Communication API Test

1. Build and Installation

   Refer to the Communication API test build and installation section in [getting_started.md](https://github.com/whollo/FlagCX/blob/add-flagcx-wuh/docs/getting_started.md).

2. Communication API Test

   ```Plain
   mpirun --allow-run-as-root -np 2 ./test_allreduce -b 128K -e 4G -f 2 -p 1
   ```

   **Description**

   -  `test_allreduce` is a performance benchmark for AllReduce operations built on MPI and FlagCX. Each MPI process is bound to a single GPU. The program runs warm-up iterations followed by timed measurements across a user-defined range of message sizes (minimum, maximum, and step).
   -  For every message size, the benchmark reports:
     - Average latency
     - Estimated bandwidth
     - Buffer fragments for correctness verification

   **Example**

   - Running `test_allreduce` with 2 MPI processes on 2 GPUs starts from 128 KiB and doubles the message size each step (128 KiB, 256 KiB, 512 KiB, 1 MiB …) up to 4 GiB. For each size, the benchmark records bandwidth, latency, and correctness results.

3. Correct Performance Test Output

   [Correct_Performance_Test_Output.PNG](https://github.com/whollo/FlagCX/blob/add-flagcx-wuh/docs/images/Correct_Performance_Test_Output.PNG)


4. Issues Encountered During Execution

   - During execution, you may see an assertion warning when OpenMPI attempts to establish a connection via InfiniBand (openib BTL) but cannot find an available CPC (Connection Protocol). In this case, the IB port is disabled automatically.This warning does not affect the performance test results.

     [Issues_Encountered_During_Execution.PNG](https://github.com/whollo/FlagCX/blob/add-flagcx-wuh/docs/images/Issues_Encountered_During_Execution.PNG)

     **Solution**

     To suppress this warning, disable `openib` and fall back to TCP by adding the following option to your `mpirun` command。

     ```Plain
     --mca btl ^openib
     ```

   - **MPI Error Warning**

     If you encounter an MPI error during execution, there are two possible solutions:

     **Check Local MPI Installation**

     - Verify your local MPI installation path and set the appropriate environment variables.

     **Install MPI**

     - If MPI is not installed or the local installation is not suitable, download and install MPI.

     - Follow the instructions below:

       ```Plain
       wget https://download.open-mpi.org/release/open-mpi/v4.1/openmpi-4.1.6.tar.gz  
       tar -zxf openmpi-4.1.6.tar.gz  
       cd openmpi-4.1.6  
       ##Configure and Build 
       ./configure --prefix=/usr/local/mpi make -j$(nproc) sudo make install
       ```


5. Test Results

| Test Machine     | Communication Library | Communication Bandwidth (GB/s)                               |
| ---------------- | --------------------- | ------------------------------------------------------------ |
|                  |                       | AllReduce                                                    |
| Nvidia A800*2    | FlagCX                | Comm size: 131072 bytes; Elapsed time: 0.000015 sec; Algo bandwidth: 8.731905 GB/s; Bus bandwidth: 8.731905 GB/s Comm size: 262144 bytes; Elapsed time: 0.000017 sec; Algo bandwidth: 15.087099 GB/s; Bus bandwidth: 15.087099 GB/s Comm size: 524288 bytes; Elapsed time: 0.000023 sec; Algo bandwidth: 23.131671 GB/s; Bus bandwidth: 23.131671 GB/s Comm size: 1048576 bytes; Elapsed time: 0.000033 sec; Algo bandwidth: 32.190260 GB/s; Bus bandwidth: 32.190260 GB/s Comm size: 2097152 bytes; Elapsed time: 0.000058 sec; Algo bandwidth: 36.264304 GB/s; Bus bandwidth: 36.264304 GB/s Comm size: 4194304 bytes; Elapsed time: 0.000074 sec; Algo bandwidth: 57.054144 GB/s; Bus bandwidth: 57.054144 GB/s Comm size: 8388608 bytes; Elapsed time: 0.000113 sec; Algo bandwidth: 74.233794 GB/s; Bus bandwidth: 74.233794 GB/s Comm size: 16777216 bytes; Elapsed time: 0.000171 sec; Algo bandwidth: 98.392462 GB/s; Bus bandwidth: 98.392462 GB/s Comm size: 33554432 bytes; Elapsed time: 0.000303 sec; Algo bandwidth: 110.589415 GB/s; Bus bandwidth: 110.589415 GB/s Comm size: 67108864 bytes; Elapsed time: 0.000554 sec; Algo bandwidth: 121.142415 GB/s; Bus bandwidth: 121.142415 GB/s Comm size: 134217728 bytes; Elapsed time: 0.001055 sec; Algo bandwidth: 127.273969 GB/s; Bus bandwidth: 127.273969 GB/s Comm size: 268435456 bytes; Elapsed time: 0.002030 sec; Algo bandwidth: 132.228867 GB/s; Bus bandwidth: 132.228867 GB/s Comm size: 536870912 bytes; Elapsed time: 0.003927 sec; Algo bandwidth: 136.727231 GB/s; Bus bandwidth: 136.727231 GB/s Comm size: 1073741824 bytes; Elapsed time: 0.007598 sec; Algo bandwidth: 141.313472 GB/s; Bus bandwidth: 141.313472 GB/s Comm size: 2147483648 bytes; Elapsed time: 0.014943 sec; Algo bandwidth: 143.715275 GB/s; Bus bandwidth: 143.715275 GB/s Comm size: 4294967296 bytes; Elapsed time: 0.029790 sec; Algo bandwidth: 144.176214 GB/s; Bus bandwidth: 144.176214 GB/s |
| Muxi C550*2      | FlagCX                | Comm size: 131072 bytes; Elapsed time: 0.000050 sec; Algo bandwidth: 2.634695 GB/s; Bus bandwidth: 2.634695 GB/s Comm size: 262144 bytes; Elapsed time: 0.000031 sec; Algo bandwidth: 8.369632 GB/s; Bus bandwidth: 8.369632 GB/s Comm size: 524288 bytes; Elapsed time: 0.000040 sec; Algo bandwidth: 13.212961 GB/s; Bus bandwidth: 13.212961 GB/s Comm size: 1048576 bytes; Elapsed time: 0.000054 sec; Algo bandwidth: 19.402579 GB/s; Bus bandwidth: 19.402579 GB/s Comm size: 2097152 bytes; Elapsed time: 0.000083 sec; Algo bandwidth: 25.262600 GB/s; Bus bandwidth: 25.262600 GB/s Comm size: 4194304 bytes; Elapsed time: 0.000140 sec; Algo bandwidth: 29.938154 GB/s; Bus bandwidth: 29.938154 GB/s Comm size: 8388608 bytes; Elapsed time: 0.000253 sec; Algo bandwidth: 33.159476 GB/s; Bus bandwidth: 33.159476 GB/s Comm size: 16777216 bytes; Elapsed time: 0.000476 sec; Algo bandwidth: 35.209899 GB/s; Bus bandwidth: 35.209899 GB/s Comm size: 33554432 bytes; Elapsed time: 0.000927 sec; Algo bandwidth: 36.207014 GB/s; Bus bandwidth: 36.207014 GB/s Comm size: 67108864 bytes; Elapsed time: 0.001825 sec; Algo bandwidth: 36.773992 GB/s; Bus bandwidth: 36.773992 GB/s Comm size: 134217728 bytes; Elapsed time: 0.002861 sec; Algo bandwidth: 46.908989 GB/s; Bus bandwidth: 46.908989 GB/s Comm size: 268435456 bytes; Elapsed time: 0.005688 sec; Algo bandwidth: 47.192213 GB/s; Bus bandwidth: 47.192213 GB/s Comm size: 536870912 bytes; Elapsed time: 0.011349 sec; Algo bandwidth: 47.305300 GB/s; Bus bandwidth: 47.305300 GB/s Comm size: 1073741824 bytes; Elapsed time: 0.022698 sec; Algo bandwidth: 47.306069 GB/s; Bus bandwidth: 47.306069 GB/s Comm size: 2147483648 bytes; Elapsed time: 0.045399 sec; Algo bandwidth: 47.302873 GB/s; Bus bandwidth: 47.302873 GB/s Comm size: 4294967296 bytes; Elapsed time: 0.090738 sec; Algo bandwidth: 47.333872 GB/s; Bus bandwidth: 47.333872 GB/s |
| Kunlunxin P800*2 | FlagCX                | Comm size: 131072 bytes; Elapsed time: 0.000024 sec; Algo bandwidth: 5.400883 GB/s; Bus bandwidth: 5.400883 GB/s Comm size: 262144 bytes; Elapsed time: 0.000028 sec; Algo bandwidth: 9.322930 GB/s; Bus bandwidth: 9.322930 GB/s Comm size: 524288 bytes; Elapsed time: 0.000039 sec; Algo bandwidth: 13.533698 GB/s; Bus bandwidth: 13.533698 GB/s Comm size: 1048576 bytes; Elapsed time: 0.000065 sec; Algo bandwidth: 16.166061 GB/s; Bus bandwidth: 16.166061 GB/s Comm size: 2097152 bytes; Elapsed time: 0.000119 sec; Algo bandwidth: 17.645851 GB/s; Bus bandwidth: 17.645851 GB/s Comm size: 4194304 bytes; Elapsed time: 0.000225 sec; Algo bandwidth: 18.643650 GB/s; Bus bandwidth: 18.643650 GB/s Comm size: 8388608 bytes; Elapsed time: 0.000436 sec; Algo bandwidth: 19.261398 GB/s; Bus bandwidth: 19.261398 GB/s Comm size: 16777216 bytes; Elapsed time: 0.000854 sec; Algo bandwidth: 19.643811 GB/s; Bus bandwidth: 19.643811 GB/s Comm size: 33554432 bytes; Elapsed time: 0.001692 sec; Algo bandwidth: 19.827786 GB/s; Bus bandwidth: 19.827786 GB/s Comm size: 67108864 bytes; Elapsed time: 0.003368 sec; Algo bandwidth: 19.926382 GB/s; Bus bandwidth: 19.926382 GB/s Comm size: 134217728 bytes; Elapsed time: 0.006723 sec; Algo bandwidth: 19.964725 GB/s; Bus bandwidth: 19.964725 GB/s Comm size: 268435456 bytes; Elapsed time: 0.013417 sec; Algo bandwidth: 20.006629 GB/s; Bus bandwidth: 20.006629 GB/s Comm size: 536870912 bytes; Elapsed time: 0.026818 sec; Algo bandwidth: 20.019345 GB/s; Bus bandwidth: 20.019345 GB/s Comm size: 1073741824 bytes; Elapsed time: 0.053621 sec; Algo bandwidth: 20.024734 GB/s; Bus bandwidth: 20.024734 GB/s Comm size: 2147483648 bytes; Elapsed time: 0.107230 sec; Algo bandwidth: 20.026859 GB/s; Bus bandwidth: 20.026859 GB/s Comm size: 4294967296 bytes; Elapsed time: 0.214454 sec; Algo bandwidth: 20.027448 GB/s; Bus bandwidth: 20.027448 GB/s |

### Torch API Test

1. Build and Installation

   Refer to [getting_started.md](https://github.com/whollo/FlagCX/blob/add-flagcx-wuh/docs/getting_started.md) for instructions on building and installing the Torch API test.

2. Torch API Test Execution

   - The test case is located in the build/installation directory.

     ```Plain
     cd ./example/example.py
     ```

   - The test script `run.sh` sets environment variables and device IDs according to the current platform. You may need to modify these variables to match your hardware setup.

     ```Plain
     ##run.sh
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
     
     export FLAGCX_IB_HCA=mlx5
     export FLAGCX_ENABLE_TOPO_DETECT=TRUE
     export FLAGCX_DEBUG=TRUE
     export FLAGCX_DEBUG_SUBSYS=ALL
     export CUDA_VISIBLE_DEVICES=0,1
     # Need to preload customized gloo library specified for FlagCX linkage
     # export LD_PRELOAD=/usr/local/lib/libgloo.so
     # export LD_PRELOAD=/usr/local/nccl/build/lib/libnccl.so
     export TORCH_DISTRIBUTED_DETAIL=DEBUG
     CMD='torchrun --nproc_per_node 2 --nnodes=1 --node_rank=0 --master_addr="localhost" --master_port=8281 example.py'
     
     echo $CMD
     eval $CMD
     ```

      **Explanation**

       `CMD='torchrun --nproc_per_node 2 --nnodes=1 --node_rank=0 --master_addr="``localhost``" --master_port=8281 example.py'`

     - `--nproc_per_node 2`: Launch 2 processes on the current machine.
     - `--nnodes 1`: Total number of nodes participating in the training. For homogeneous testing, set to 1.
     - `--node_rank 0`: Rank of the current node among all nodes, starting from 0. For homogeneous testing, fixed at 0.
     - `--master_addr "``localhost``"`: Address of the master node. For homogeneous testing, `localhost` is sufficient; for heterogeneous testing, specify the reachable IP or hostname of the master node, accessible by all nodes.
     - `--master_port 8281`: Port used by the master node to establish the process group. All nodes must use the same port, which must be free.
     - `example.py`: Torch API test script.
     - Refer to [enviroment_variables.md](https://github.com/whollo/FlagCX/blob/add-flagcx-wuh/docs/enviroment_variables.md) for the meaning and usage of `FLAGCX_XXX` environment variables.

3. Sample Screenshot of Correct Performance Test

   [Sample_Screenshot_of_Correct_Performance_Test.png](https://github.com/whollo/FlagCX/blob/add-flagcx-wuh/docs/images/Sample_Screenshot_of_Correct_Performance_Test.png)

4. Test Results

| Test Machine     | Communication Library | Torch API Test — AllReduce                                   |
| ---------------- | --------------------- | ------------------------------------------------------------ |
| Nvidia A800*2    | FlagCX                | rank 1 before allreduce: x = tensor([0.8574, 0.7943], device='cuda:1'), y = tensor([0.8362, 0.6185], device='cuda:1') rank 0 before allreduce: x = tensor([0.1592, 0.4747], device='cuda:0'), y = tensor([0.2146, 0.8306], device='cuda:0') rank 1 after allreduce min with FLAGCX_GROUP1: x = tensor([0.1592, 0.4747], device='cuda:1') rank 0 after allreduce min with FLAGCX_GROUP1: x = tensor([0.1592, 0.4747], device='cuda:0') rank 1 after allreduce max with FLAGCX_GROUP1: y = tensor([0.8362, 0.8306], device='cuda:1') rank 0 after allreduce max with FLAGCX_GROUP1: y = tensor([0.8362, 0.8306], device='cuda:0') rank 1 after allreduce sum with FLAGCX_GROUP1: x = tensor([0.3184, 0.9495], device='cuda:1') rank 0 after allreduce sum with FLAGCX_GROUP1: x = tensor([0.3184, 0.9495], device='cuda:0') |
| Muxi C550*2      | FlagCX                | rank 1 before allreduce: x = tensor([0.4192, 0.7684], device='cuda:1'), y = tensor([0.0078, 0.9437], device='cuda:1') rank 0 before allreduce: x = tensor([0.6049, 0.0235], device='cuda:0'), y = tensor([0.9357, 0.1013], device='cuda:0') rank 0 after allreduce min with FLAGCX_GROUP1: x = tensor([0.4192, 0.0235], device='cuda:0') rank 1 after allreduce min with FLAGCX_GROUP1: x = tensor([0.4192, 0.0235], device='cuda:1') rank 0 after allreduce max with FLAGCX_GROUP1: y = tensor([0.9357, 0.9437], device='cuda:0') rank 1 after allreduce max with FLAGCX_GROUP1: y = tensor([0.9357, 0.9437], device='cuda:1') rank 0 after allreduce sum with FLAGCX_GROUP1: x = tensor([0.8385, 0.0469], device='cuda:0') rank 1 after allreduce sum with FLAGCX_GROUP1: x = tensor([0.8385, 0.0469], device='cuda:1') |
| Kunlunxin P800*2 | FlagCX                | rank 0 before allreduce: x = tensor([0.6889, 0.6838], device='cuda:0'), y = tensor([0.9107, 0.1798], device='cuda:0') rank 1 before allreduce: x = tensor([0.3769, 0.3887], device='cuda:1'), y = tensor([0.5182, 0.2783], device='cuda:1') rank 0 after allreduce min with FLAGCX_GROUP1: x = tensor([0.3769, 0.3887], device='cuda:0') rank 1 after allreduce min with FLAGCX_GROUP1: x = tensor([0.3769, 0.3887], device='cuda:1') rank 0 after allreduce max with FLAGCX_GROUP1: y = tensor([0.9107, 0.2783], device='cuda:0') rank 1 after allreduce max with FLAGCX_GROUP1: y = tensor([0.9107, 0.2783], device='cuda:1') rank 0 after allreduce sum with FLAGCX_GROUP1: x = tensor([0.7538, 0.7773], device='cuda:0') rank 1 after allreduce sum with FLAGCX_GROUP1: x = tensor([0.7538, 0.7773], device='cuda:1') |
|                  |                       |                                                              |

### FlagCX LLaMA3-8B Training on NVIDIA GPUs using FlagScale

1. Build and Installation

   Refer to the Environment Setup and Build & Installation sections in [getting_started.md](https://github.com/whollo/FlagCX/blob/add-flagcx-wuh/docs/getting_started.md) under FlagScale with NVIDIA GPUs + FlagCX LLaMA3-8B Training.

2. Data Preparation and Model Configuration

   - **Data Preparation**

     ```
     cd FlagScale
     mkdir data
     ```

     **Description** :A small portion of processed data from the Pile dataset (bin and idx files) is provided: pile_wikipedia_demo.Copy it to the FlagScale/data directory.

   - **Model Configuration 1**

     ```
     cd FlagScale/examples/llama3/conf/ 
     vi train.yaml
     ```

     **Description** The directory contains the following files:

     - `train/` — Training scripts and related files

     - `train.yaml` — Configuration file for **homogeneous training**

       The `train.yaml` file contains four main sections: defaults, experiment, action, and hydra. For most cases, you only need to modify defaults and experiment.

       - Modify `defaults`

         ```
         train: XXXX`
         ```

          Replace `XXXX` with `8b`.

       - Modify `experiment`

         ```
         exp_dir: ./outputs_llama3_8b
         ```

         This specifies the output directory for distributed training results.

       - Modify `runner` settings under `experiment`

         ​    **hostfile**: Since this is a homogeneous (single-node) test, comment out the `hostfile` line. Only configure it for heterogeneous (multi-node) setups.

         ​    **envs**: Set GPU device IDs using `CUDA_VISIBLE_DEVICES`, for example:

         ```
         CUDA_VISIBLE_DEVICES: 0,1,2,3,4,5,6,7
         ```

     - `train_hetero.yaml` — Configuration file for **heterogeneous training**

   - **Model Configuration 2**

     ```Plain
     # Multiple model configuration files (xxx.yaml) corresponding to different dataset sizes in this directory
     cd FlagScale/examples/llama3/conf/train 
     vi 8b.yaml 
     ```

     - **`8b.yaml`** **Configuration File**

       The `8b.yaml` file contains three main sections: system, model, and data.

       **System Section**

       Add the following line to enable distributed training with FlagCX:

       ```Plain
       distributed_backend: flagcx
       ```

       **Model Section**

       Configure the training parameters.Use `train_samples` and `global_batch_size` to determine the number of steps:

       ```Plain
       step = train_samples / global_batch_size
       ```

         It is recommended to set it as an integer.

       **Data Section**

       Modify the following parameters:

       - **data_path**: Set this to the `cache` directory under the data prepared in the previous step.

       - **tokenizer_path**: Download the tokenizer from the official website corresponding to your model and set the path here.

   - **Tokenizer Download**

     **Description:**

     Download the tokenizer corresponding to the model. The files are available at: [Meta-LLaMA-3-8B-Instruct Tokenizer](https://www.modelscope.cn/models/LLM-Research/Meta-Llama-3-8B-Instruct/files?utm_source=chatgpt.com).

     **Instructions:**

     - It is recommended to download the tokenizer via the command line.

     - Place the downloaded tokenizer files in the path specified by `tokenizer_path` in your configuration (`8b.yaml`).

     **Example:**

     ```Plain
     ## Download the tokenizer to the current directory
     cd FlagScale/examples/llama3
     modelscope download --model LLM-Research/Meta-Llama-3-8B-Instruct [XXXX] --local_dir ./
     ```

     **Description**

     `[XXXX]` refers to the tokenizer files corresponding to Meta-LLaMA-3-8B-Instruct, for example:

     - `tokenizer.json`
     - `tokenizer_config.json`
     - `config.json`
     - `configuration.json`
     - `generation_config.json`

     These files should be placed in the directory specified by `tokenizer_path` in your configuration (`8b.yaml`).

3. Training Backend Code Adaptation for Unpatched Mode

   ```Plain
   cd FlagScale
   python tools/patch/unpatch.py --backend Megatron-LM
   ```

4. Distributed Training

   ```Plain
   cd FlagScale
   ##Start Distributed Training
   python run.py --config-path ./examples/llama3/conf --config-name train action=run 
   ## Stop Distributed Training
   python run.py --config-path ./examples/llama3/conf --config-name train action=stop 
   ```

   After starting distributed training, the configuration information will be printed, and a run script will be generated at:

   ```Plain
   /flagscale/outputs_llama3_8b/logs/scripts/host_0_localhost_run.sh`
   ```

   The training output files can be found in:

   ```Plain
   /flagscale/outputs_llama3_8b
   ```

   **Notes:**

   - You can inspect the run script to verify the commands and environment settings used for the training.

   - All logs and model checkpoints will be saved under the output directory.

     [Distributed_Training.png](https://github.com/whollo/FlagCX/blob/add-flagcx-wuh/docs/images/Distributed_Training.png)

## Heterogeneous Tests Using FlagCX

### Communication API Test

1. Build and Installation

   Refer to the Environment Setup, Creating Symbolic Links, and Build & Installation sections in [getting_started.md](https://github.com/whollo/FlagCX/blob/add-flagcx-wuh/docs/getting_started.md) under Heterogeneous Communication API Test.

2. Verify MPICH Installation

   ```Plain
   # Check if MPICH is installed
   cd /workspace/mpich-4.2.3
   ```

3. Makefile and Environment Variable Configuration

   ```
   # Navigate to the Communication API test directory
   cd /root/FlagCX/test/perf 
   
   # Open the Makefile
   vi Makefile
       # Modify the MPI path to match the one used in step (3)
       MPI_HOME ?= /workspace/mpich-4.2.3/build/ 
   :q # Save and exit
   
   # Configure environment variables
   export LD_LIBRARY_PATH=/workspace/mpich-4.2.3/build/lib:$LD_LIBRARY_PATH
   ```

4. Heterogeneous Communication API Test

   - Ensure that Host 1, Host 2, … are all configured as described above and can correctly run the homogeneous Communication API test on their respective platforms.


   - Verify that the ports on Host 1, Host 2, … are `<xxx>` and keep them consistent across all hosts.


   - Before running the heterogeneous Communication API test script on Host 1, configure the port number environment variable:

     ```Plain
     export HYDRA_LAUNCHER_EXTRA_ARGS="-p 8010"
     ```
     
     Here, `8010` should match the configuration set during SSH passwordless login.

   - Run the heterogeneous Communication API test script on Host 1:

     ```Plain
     ./run.sh
     ```

     ```Plain
     ##Nvida A800*1 + Muxi C550*1
     /workspace/mpich-4.2.3/build/bin/mpirun \
       -np 2 -hosts 10.1.15.233:1,10.1.15.67:1 \
       -env PATH=/workspace/mpich-4.2.3/build/bin \
       -env LD_LIBRARY_PATH=/workspace/mpich-4.2.3/build/lib:/root/FlagCX/build/lib:/usr/local/mpi/lib/:/opt/maca/ompi/lib \
       -env FORCE_ACTIVE_WAIT=2 \
       -env FLAGCX_IB_HCA=mlx5 \
       -env FLAGCX_ENABLE_TOPO_DETECT=TRUE \
       -env FLAGCX_DEBUG=INFO \
       -env FLAGCX_DEBUG_SUBSYS=INIT \
       -env CUDA_VISIBLE_DEVICES=1 \
       -env MACA_VISIBLE_DEVICES=3 \
       /root/FlagCX/test/perf/test_allreduce -b 128K -e 4G -f 2 -w 5 -n 100 -p 1`
     ```

     - Refer to [enviroment_variables.md](https://github.com/whollo/FlagCX/blob/add-flagcx-wuh/docs/enviroment_variables.md) for the meaning and usage of `FLAGCX_XXX` environment variables.


   - **Note:** When using two GPUs per node in the heterogeneous Communication API test, some warnings may indicate that each node only has 1 GPU active. In this case, FlagCX will skip GPU-to-GPU AllReduce and fall back to host-based communication.

     - As a result, GPU utilization may show 0%, and the overall AllReduce runtime may be much longer.
     
     - However, the computation results are correct, and this behavior is expected.

     - To fully utilize GPU acceleration for heterogeneous testing, use 2+2 GPUs (4 GPUs total) across the nodes.
     
       ![Heterogeneous_Communication_API_Test.png](https://github.com/whollo/FlagCX/blob/add-flagcx-wuh/docs/images/Heterogeneous_Communication_API_Test.png)
     
       ![Heterogeneous_Communication_API_Test.png](docs/images/Heterogeneous_Communication_API_Test.png)
     


5. Test Results

| Test Machine                     | Communication Library | Communication Bandwidth (GB/s)                               |
| -------------------------------- | --------------------- | ------------------------------------------------------------ |
|                                  |                       | AllReduce                                                    |
| Nvidia A800 * 2 + Metax C550 * 2 | FlagCX                | Comm size: 131072 bytes; Elapsed time: 0.008623 sec; Algo bandwidth: 0.015200 GB/s; Bus bandwidth: 0.022800 GB/s Comm size: 262144 bytes; Elapsed time: 0.010125 sec; Algo bandwidth: 0.025891 GB/s; Bus bandwidth: 0.038836 GB/s Comm size: 524288 bytes; Elapsed time: 0.009634 sec; Algo bandwidth: 0.054418 GB/s; Bus bandwidth: 0.081627 GB/s Comm size: 1048576 bytes; Elapsed time: 0.009137 sec; Algo bandwidth: 0.114764 GB/s; Bus bandwidth: 0.172146 GB/s  Comm size: 2097152 bytes; Elapsed time: 0.009613 sec; Algo bandwidth: 0.218167 GB/s; Bus bandwidth: 0.327251 GB/s Comm size: 4194304 bytes; Elapsed time: 0.012051 sec; Algo bandwidth: 0.348057 GB/s; Bus bandwidth: 0.522086 GB/s Comm size: 8388608 bytes; Elapsed time: 0.010939 sec; Algo bandwidth: 0.766843 GB/s; Bus bandwidth: 1.150265 GB/s Comm size: 16777216 bytes; Elapsed time: 0.015018 sec; Algo bandwidth: 1.117159 GB/s; Bus bandwidth: 1.675738 GB/s Comm size: 33554432 bytes; Elapsed time: 0.014919 sec; Algo bandwidth: 2.249136 GB/s; Bus bandwidth: 3.373704 GB/s Comm size: 67108864 bytes; Elapsed time: 0.015826 sec; Algo bandwidth: 4.240359 GB/s; Bus bandwidth: 6.360538 GB/s Comm size: 134217728 bytes; Elapsed time: 0.019956 sec; Algo bandwidth: 6.725734 GB/s; Bus bandwidth: 10.088601 GB/s Comm size: 268435456 bytes; Elapsed time: 0.030975 sec; Algo bandwidth: 8.666267 GB/s; Bus bandwidth: 12.999401 GB/s Comm size: 536870912 bytes; Elapsed time: 0.051275 sec; Algo bandwidth: 10.470417 GB/s; Bus bandwidth: 15.705625 GB/s Comm size: 1073741824 bytes; Elapsed time: 0.082816 sec; Algo bandwidth: 12.965361 GB/s; Bus bandwidth: 19.448042 GB/s Comm size: 2147483648 bytes; Elapsed time: 0.142399 sec; Algo bandwidth: 15.080749 GB/s; Bus bandwidth: 22.621124 GB/s Comm size: 4294967296 bytes; Elapsed time: 0.268710 sec; Algo bandwidth: 15.983634 GB/s; Bus bandwidth: 23.975451 GB/s |
|                                  |                       |                                                              |