# 一、环境配置

容器创建参考命令：

```Plain
-------------Nvidia A800平台上容器创建命令参考---------------------------------------
sudo docker run -itd \
                --name flagscale-flagcx-test \
                --privileged \
                --net=host \
                --pid=host \
                --cap-add=ALL \
                --shm-size 128G \
                --ulimit memlock=-1 \
                --gpus all \
                -v /dev/:/dev/ \
                -v /usr/src/:/usr/src/ \
                -v /lib/modules/:/lib/modules/ \
                -v /nfs/changtao/:/workspace/ \
                flagscale_xlc:cuda12.4.1-cudnn9.5.0-python3.12-torch2.6.0-time2504161115-ssh \
                /bin/bash
                
-------------Kunlunxin P800平台上容器创建命令参考--------------------------------------- 
sudo docker run -itd \
        --name flagcx-test-wuh \
        --privileged \
        --net=host \
        --pid=host \
        --shm-size 128G \
        --ulimit memlock=-1 \
        --group-add video \
        -v /public-nvme/changtao/:/root/workspace \
        -v /usr/local/xpu/:/usr/local/xpu \
        flagcx_kunlunxin_base:v1.1 \
        /bin/bash              
```

# 二、FlagCX安装编译

## 1.获取FlagCX源码和安装编译

查看并选择匹配当前运行平台的编译选项

```Plain
git clone https://github.com/FlagOpen/FlagCX.git
cd FlagCX 
cat Makefile
make USE_NVIDIA=1 -j$(nproc) # NVIDIA GPU 平台
make USE_CAMBRICON=1 -j$(nproc)  # 寒武纪 CAMBRICON 平台
make USE_KUNLUNXIN=1 -j$(nproc) #昆仑芯平台
make USE_NVIDIA=1 USE_CAMBRICON=1 ... -j$(nproc) #多平台混合编译（如同时支持 NVIDIA 和寒武纪等）
```

## 2.正确编译结果

![img](https://jwolpxeehx.feishu.cn/space/api/box/stream/download/asynccode/?code=MzdkNjQ2OTg3M2E3MzlkYjc5OTRkM2U2NDkxMThlYzVfdDlSaHpnaHdOQjgyN09rNW9XSWRpbGtWc0tLbGpJdDlfVG9rZW46U2FIbGJaNVNwb0ExUjl4WkdxdmNBeTlMbnVkXzE3NTYyNzk1NjE6MTc1NjI4MzE2MV9WNA)

## 3.编译过程中可能遇到的问题

### （1）若提示找不到nccl.h等库

- 可以先locate xxx.h查看本地路径，找到后在Makefile中对应库调用路径上进行替换即可。本地没有再进行对应的头文件库安装（安装方法很多，下面提供其中一种）。

```Plain
sudo apt-key adv --keyserver keyserver.ubuntu.com --recv-keys A4B469963BF863CC  #导入公钥
echo "deb https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2004/x86_64/ /" | sudo tee /etc/apt/sources.list.d/cuda.list #添加仓库
sudo apt-get update #更新apt
sudo apt-get install libnccl-dev #安装NCCL开发包
sudo apt-get install cuda-toolkit-11-8 #安装 CUDA Toolkit
```

### （2）若提示gtest.h头文件错误

- 需安装gtest单元测试框架，安装如下：

```Plain
git clone https://github.com/google/googletest.git #获取gtest源码
cd googletest #进入项目目录
mkdir build #创建一个目录来存放构建输出结果
cd build #进入build目录
cmake ..  #为GoogleTest生成原生构建脚本
make 
sudo make install #安装静态库
```

# 三、使用FlagCX进行同构测试

## 1.通信API测试

### （1）环境准备

- 选择镜像 `flagscale_xlc:cuda12.4.1-cudnn9.5.0-python3.12-torch2.6.0-time2504161115-ssh`创建docker容器并进入。

### （2）选择编译选项

```Plain
cd FlagCX/test/perf #进入测试目录
make USE_NVIDIA=1 #根据所在硬件平台选择编译选项
```

### （3）正确编译结果

![img](https://jwolpxeehx.feishu.cn/space/api/box/stream/download/asynccode/?code=YzQyMzZkOWM2Y2E4MDYyOWQ2YzdjMWQxYWJkNTZkZjBfbE1IMm9DQlN0YnRCV1FyNHNxQUpFb0hKakVVeVZEZUxfVG9rZW46RlZ6a2JqZkFOb0x5ME94RmZrRmNmTWlHbktmXzE3NTYyNzQ4ODE6MTc1NjI3ODQ4MV9WNA)

### （4）通信API测试

```Plain
mpirun --allow-run-as-root -np 2 ./test_allreduce -b 128K -e 4G -f 2 -p 1
----------说明---------------
test_allreduce是一个基于 MPI 与 FlagCX 的 AllReduce 性能基准程序：它将每个 MPI 进程绑定到一块 GPU，根据用户指定的最小/最大消息大小和步长对 AllReduce 操作先做热身再进行正式测量，并在每种消息大小下打印平均耗时、带宽估算以及用于正确性检查的缓冲区片段。例如，用 2 个 MPI 进程2张加速卡运行 test_allreduce 时，测试会从 128 KiB 起按 2 倍递增（128 KiB、256 KiB、512 KiB、1 MiB …）直至 4 GiB，分别记录每个消息尺寸下的带宽、延迟与正确性信息。
```

### （5）正确的性能测试截图

![img](https://jwolpxeehx.feishu.cn/space/api/box/stream/download/asynccode/?code=YTJjMzUzN2U4YzBkZTBjZDQ0ODM2MDE4NTMwOGNkMGNfREhTWHVOQ2NmOHc5cFFRekJzQVoweW1GeUQ5bnlKdTFfVG9rZW46U2FIbGJaNVNwb0ExUjl4WkdxdmNBeTlMbnVkXzE3NTYyNzk2MjU6MTc1NjI4MzIyNV9WNA)

### （6）运行过程中遇到的问题：

- 运行时可能会出现一个断言提示，OpenMPI 试图用 InfiniBand（openib BTL）建立连接时，没有找到可用的 CPC（连接协议），所以直接把这个 IB 端口禁用了。但是不影响性能测试结果。（如果想要去掉这个断言提示，可以在mpi运行指令中通过添加--mca btl ^openib直接禁用openib，走TCP。）

  ![img](https://jwolpxeehx.feishu.cn/space/api/box/stream/download/asynccode/?code=NTEyYWE2MGZjM2U0N2RlYzVmZWIwOTZjODEwY2Q3MjNfWk5FQ1BEWHVjUVViU2JGME01c0lYVEt3QXpmR0cwSk5fVG9rZW46Tm53UWJ5NVV1b2EyMGl4MGlVR2NkUnZVbjZlXzE3NTYyNzk2MzM6MTc1NjI4MzIzM19WNA)

- 提示MPI错误，两种解决方案，一种查看本地mpi安装路径，设置环境变量，参考enviroment_variables.md；一种是下载安装，如下：

  ```Plain
  wget https://download.open-mpi.org/release/open-mpi/v4.1/openmpi-4.1.6.tar.gz #获取源码
  tar -zxf openmpi-4.1.6.tar.gz #解压压缩包
  cd openmpi-4.1.6 #进入项目目录
  配置并编译
  ./configure --prefix=/usr/local/mpi
  make -j$(nproc)
  sudo make install
  ```

### （7）测试结果

| 通信库           | 测试机器 | 通信带宽（GB/s）                                             |
| ---------------- | -------- | ------------------------------------------------------------ |
|                  |          | AllReduce                                                    |
| Nvidia A800*2    | FlagCX   | Comm size: 131072 bytes; Elapsed time: 0.000015 sec; Algo bandwidth: 8.731905 GB/s; Bus bandwidth: 8.731905 GB/s Comm size: 262144 bytes; Elapsed time: 0.000017 sec; Algo bandwidth: 15.087099 GB/s; Bus bandwidth: 15.087099 GB/s Comm size: 524288 bytes; Elapsed time: 0.000023 sec; Algo bandwidth: 23.131671 GB/s; Bus bandwidth: 23.131671 GB/s Comm size: 1048576 bytes; Elapsed time: 0.000033 sec; Algo bandwidth: 32.190260 GB/s; Bus bandwidth: 32.190260 GB/s Comm size: 2097152 bytes; Elapsed time: 0.000058 sec; Algo bandwidth: 36.264304 GB/s; Bus bandwidth: 36.264304 GB/s Comm size: 4194304 bytes; Elapsed time: 0.000074 sec; Algo bandwidth: 57.054144 GB/s; Bus bandwidth: 57.054144 GB/s Comm size: 8388608 bytes; Elapsed time: 0.000113 sec; Algo bandwidth: 74.233794 GB/s; Bus bandwidth: 74.233794 GB/s Comm size: 16777216 bytes; Elapsed time: 0.000171 sec; Algo bandwidth: 98.392462 GB/s; Bus bandwidth: 98.392462 GB/s Comm size: 33554432 bytes; Elapsed time: 0.000303 sec; Algo bandwidth: 110.589415 GB/s; Bus bandwidth: 110.589415 GB/s                                                                                                                                                                                                                               Comm size: 67108864 bytes; Elapsed time: 0.000554 sec; Algo bandwidth: 121.142415 GB/s; Bus bandwidth: 121.142415 GB/s                                                                                                                                                                                                                              Comm size: 134217728 bytes; Elapsed time: 0.001055 sec; Algo bandwidth: 127.273969 GB/s; Bus bandwidth: 127.273969 GB/s                                                                                                                                                                                                                 Comm size: 268435456 bytes; Elapsed time: 0.002030 sec; Algo bandwidth: 132.228867 GB/s; Bus bandwidth: 132.228867 GB/s                                                                                                                                                                                                            Comm size: 536870912 bytes; Elapsed time: 0.003927 sec; Algo bandwidth: 136.727231 GB/s; Bus bandwidth: 136.727231 GB/s                                                                                                                                                                                                             Comm size: 1073741824 bytes; Elapsed time: 0.007598 sec; Algo bandwidth: 141.313472 GB/s; Bus bandwidth: 141.313472 GB/s                                                                                                                                                                                                                   Comm size: 2147483648 bytes; Elapsed time: 0.014943 sec; Algo bandwidth: 143.715275 GB/s; Bus bandwidth: 143.715275 GB/s                                                                                                                                                                                                                 Comm size: 4294967296 bytes; Elapsed time: 0.029790 sec; Algo bandwidth: 144.176214 GB/s; Bus bandwidth: 144.176214 GB/s |
| Muxi C550*2      | FlagCX   | Comm size: 131072 bytes; Elapsed time: 0.000050 sec; Algo bandwidth: 2.634695 GB/s; Bus bandwidth: 2.634695 GB/s Comm size: 262144 bytes; Elapsed time: 0.000031 sec; Algo bandwidth: 8.369632 GB/s; Bus bandwidth: 8.369632 GB/s Comm size: 524288 bytes; Elapsed time: 0.000040 sec; Algo bandwidth: 13.212961 GB/s; Bus bandwidth: 13.212961 GB/s Comm size: 1048576 bytes; Elapsed time: 0.000054 sec; Algo bandwidth: 19.402579 GB/s; Bus bandwidth: 19.402579 GB/s Comm size: 2097152 bytes; Elapsed time: 0.000083 sec; Algo bandwidth: 25.262600 GB/s; Bus bandwidth: 25.262600 GB/s Comm size: 4194304 bytes; Elapsed time: 0.000140 sec; Algo bandwidth: 29.938154 GB/s; Bus bandwidth: 29.938154 GB/s Comm size: 8388608 bytes; Elapsed time: 0.000253 sec; Algo bandwidth: 33.159476 GB/s; Bus bandwidth: 33.159476 GB/s Comm size: 16777216 bytes; Elapsed time: 0.000476 sec; Algo bandwidth: 35.209899 GB/s; Bus bandwidth: 35.209899 GB/s Comm size: 33554432 bytes; Elapsed time: 0.000927 sec; Algo bandwidth: 36.207014 GB/s; Bus bandwidth: 36.207014 GB/s Comm size: 67108864 bytes; Elapsed time: 0.001825 sec; Algo bandwidth: 36.773992 GB/s; Bus bandwidth: 36.773992 GB/s Comm size: 134217728 bytes; Elapsed time: 0.002861 sec; Algo bandwidth: 46.908989 GB/s; Bus bandwidth: 46.908989 GB/s Comm size: 268435456 bytes; Elapsed time: 0.005688 sec; Algo bandwidth: 47.192213 GB/s; Bus bandwidth: 47.192213 GB/s Comm size: 536870912 bytes; Elapsed time: 0.011349 sec; Algo bandwidth: 47.305300 GB/s; Bus bandwidth: 47.305300 GB/s Comm size: 1073741824 bytes; Elapsed time: 0.022698 sec; Algo bandwidth: 47.306069 GB/s; Bus bandwidth: 47.306069 GB/s                                                                                                                                                                                                                        Comm size: 2147483648 bytes; Elapsed time: 0.045399 sec; Algo bandwidth: 47.302873 GB/s; Bus bandwidth: 47.302873 GB/s                                                                                                                                                                                                                        Comm size: 4294967296 bytes; Elapsed time: 0.090738 sec; Algo bandwidth: 47.333872 GB/s; Bus bandwidth: 47.333872 GB/s |
| Kunlunxin P800*2 | FlagCX   | Comm size: 131072 bytes; Elapsed time: 0.000024 sec; Algo bandwidth: 5.400883 GB/s; Bus bandwidth: 5.400883 GB/s    Comm size: 262144 bytes; Elapsed time: 0.000028 sec; Algo bandwidth: 9.322930 GB/s; Bus bandwidth: 9.322930 GB/s Comm size: 524288 bytes; Elapsed time: 0.000039 sec; Algo bandwidth: 13.533698 GB/s; Bus bandwidth: 13.533698 GB/s      Comm size: 1048576 bytes; Elapsed time: 0.000065 sec; Algo bandwidth: 16.166061 GB/s; Bus bandwidth: 16.166061 GB/s Comm size: 2097152 bytes; Elapsed time: 0.000119 sec; Algo bandwidth: 17.645851 GB/s; Bus bandwidth: 17.645851 GB/s Comm size: 4194304 bytes; Elapsed time: 0.000225 sec; Algo bandwidth: 18.643650 GB/s; Bus bandwidth: 18.643650 GB/s Comm size: 8388608 bytes; Elapsed time: 0.000436 sec; Algo bandwidth: 19.261398 GB/s; Bus bandwidth: 19.261398 GB/s Comm size: 16777216 bytes; Elapsed time: 0.000854 sec; Algo bandwidth: 19.643811 GB/s; Bus bandwidth: 19.643811 GB/s Comm size: 33554432 bytes; Elapsed time: 0.001692 sec; Algo bandwidth: 19.827786 GB/s; Bus bandwidth: 19.827786 GB/s Comm size: 67108864 bytes; Elapsed time: 0.003368 sec; Algo bandwidth: 19.926382 GB/s; Bus bandwidth: 19.926382 GB/s Comm size: 134217728 bytes; Elapsed time: 0.006723 sec; Algo bandwidth: 19.964725 GB/s; Bus bandwidth: 19.964725 GB/s Comm size: 268435456 bytes; Elapsed time: 0.013417 sec; Algo bandwidth: 20.006629 GB/s; Bus bandwidth: 20.006629 GB/s Comm size: 536870912 bytes; Elapsed time: 0.026818 sec; Algo bandwidth: 20.019345 GB/s; Bus bandwidth: 20.019345 GB/s Comm size: 1073741824 bytes; Elapsed time: 0.053621 sec; Algo bandwidth: 20.024734 GB/s; Bus bandwidth: 20.024734 GB/s                                                                                                                                                                                                                         Comm size: 2147483648 bytes; Elapsed time: 0.107230 sec; Algo bandwidth: 20.026859 GB/s; Bus bandwidth: 20.026859 GB/s                                                                                                                                                                                                                Comm size: 4294967296 bytes; Elapsed time: 0.214454 sec; Algo bandwidth: 20.027448 GB/s; Bus bandwidth: 20.027448 GB/s |

## 2.Torch API测试

### （1）环境准备

- 选择镜像 `flagscale_xlc:cuda12.4.1-cudnn9.5.0-python3.12-torch2.6.0-time2504161115-ssh`创建docker容器并进入。

- 查看容器是否安装了Pytorch，如果没有需要进行安装，例如下面安装流程。

  ```Plain
  pip list #查看容器内是否安装了Pytorch
  ------------------安装Pytorch----------------------------------
  python -m pip install --pre torch \
    --index-url https://download.pytorch.org/whl/nightly/<cuXXX> \
    --trusted-host download.pytorch.org \
    --no-cache-dir --timeout 300 \
    --root-user-action=ignore
  说明：<cuXXX>表示与当前环境下的硬件驱动对应 
  nvcc --version 2>/dev/null || nvidia-smi  # 查看CUDA runtime驱动，如果release 12.4，那么就安装对应cu124版本
  ```

### （2）选择编译选项

```Plain
cd /FlagCX/plugin/torch/
python setup.py develop --adaptor [xxx]
说明：[xxx]根据当前运行平台进行选择，例如nvidia，klx，等
```

### （3）正确编译结果

- 编译完成会生成build目录，使用命令pip list |grep flagcx，可以看到flagcx对应的版本路径与当前编译所在路径一致，说明编译安装成功。

  ![img](https://jwolpxeehx.feishu.cn/space/api/box/stream/download/asynccode/?code=ODU3OThkZDg3NDAwNjY5ZDFiMDY3NjEyNjE0M2Y5MDBfTDhpcXREdEdoYUZGYjFoUGgweW44RmdtbGJpSG1EOWhfVG9rZW46VjNxUGI0dm93b0VSTTZ4V0ZmV2NSeFNjbm1oXzE3NTYyNzYxNjU6MTc1NjI3OTc2NV9WNA)

### （4）Torch API测试

- 测试用例在编译安装目录下的./example/example.py

- 测试脚本run.sh，根据当前运行平台进行更改环境变量的名称，以及使用平台硬件设备的编号。

  ```Bash
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

  - 说明：CMD='torchrun --nproc_per_node 2 --nnodes=1 --node_rank=0 --master_addr="localhost" --master_port=8281 example.py'：

  - `--nproc_per_node 2` ：表示在当前这台机器上启动 2 个进程

    `--nnodes 1`：总共参与训练的节点数，这里是同构测试就设置为1。

    `--node_rank 0`：当前这台机器在所有节点中的编号，从 0 开始。同构测试时固定为 0。

    `--master_addr "``localhost``"`：主节点地址。同构测试时用 `localhost` 就行；异构测试时必须填主节点的可达 IP/主机名，所有节点都要能连到它。

    `--master_port 8281`：主节点用于建立进程组的端口。所有节点必须一致且未被占用。

    `example.py`：Torch API测试例

### （5）正确的性能测试截图

![img](https://jwolpxeehx.feishu.cn/space/api/box/stream/download/asynccode/?code=NDdhYzg2YjJhZmM5NmMxYTkwNWMzZTNjYzc3MjNlNWZfSUZJaEhUMHpsZ0VJdWVsQW82OENKc2ZKUVNVUlVlV21fVG9rZW46TDBoeWJhRXh6b1Z2U3J4aVZSNWNPbVJJblJoXzE3NTYyNzYzNTM6MTc1NjI3OTk1M19WNA)

### （6）测试结果

| 测试机器         | 通信库 | Torch API测试--Allreduce                                     |
| ---------------- | ------ | ------------------------------------------------------------ |
| Nvidia A800*2    | FlagCX | rank 1 before allreduce: x = tensor([0.8574, 0.7943], device='cuda:1'), y = tensor([0.8362, 0.6185], device='cuda:1')               rank 0 before allreduce: x = tensor([0.1592, 0.4747], device='cuda:0'), y = tensor([0.2146, 0.8306], device='cuda:0')                 rank 1 after allreduce min with FLAGCX_GROUP1: x = tensor([0.1592, 0.4747], device='cuda:1')                                                            rank 0 after allreduce min with FLAGCX_GROUP1: x = tensor([0.1592, 0.4747], device='cuda:0')                                                         rank 1 after allreduce max with FLAGCX_GROUP1: y = tensor([0.8362, 0.8306], device='cuda:1')                                                   rank 0 after allreduce max with FLAGCX_GROUP1: y = tensor([0.8362, 0.8306], device='cuda:0')                                                         rank 1 after allreduce sum with FLAGCX_GROUP1: x = tensor([0.3184, 0.9495], device='cuda:1')                                                             rank 0 after allreduce sum with FLAGCX_GROUP1: x = tensor([0.3184, 0.9495], device='cuda:0') |
| Muxi C550*2      | FlagCX | rank 1 before allreduce: x = tensor([0.4192, 0.7684], device='cuda:1'), y = tensor([0.0078, 0.9437], device='cuda:1')                            rank 0 before allreduce: x = tensor([0.6049, 0.0235], device='cuda:0'), y = tensor([0.9357, 0.1013], device='cuda:0')                          rank 0 after allreduce min with FLAGCX_GROUP1: x = tensor([0.4192, 0.0235], device='cuda:0')                                                         rank 1 after allreduce min with FLAGCX_GROUP1: x = tensor([0.4192, 0.0235], device='cuda:1')                                                       rank 0 after allreduce max with FLAGCX_GROUP1: y = tensor([0.9357, 0.9437], device='cuda:0')                                                       rank 1 after allreduce max with FLAGCX_GROUP1: y = tensor([0.9357, 0.9437], device='cuda:1')                                                       rank 0 after allreduce sum with FLAGCX_GROUP1: x = tensor([0.8385, 0.0469], device='cuda:0')                                                             rank 1 after allreduce sum with FLAGCX_GROUP1: x = tensor([0.8385, 0.0469], device='cuda:1') |
| Kunlunxin P800*2 | FlagCX | rank 0 before allreduce: x = tensor([0.6889, 0.6838], device='cuda:0'), y = tensor([0.9107, 0.1798], device='cuda:0')                                rank 1 before allreduce: x = tensor([0.3769, 0.3887], device='cuda:1'), y = tensor([0.5182, 0.2783], device='cuda:1')                               rank 0 after allreduce min with FLAGCX_GROUP1: x = tensor([0.3769, 0.3887], device='cuda:0')                                                         rank 1 after allreduce min with FLAGCX_GROUP1: x = tensor([0.3769, 0.3887], device='cuda:1')                                                                 rank 0 after allreduce max with FLAGCX_GROUP1: y = tensor([0.9107, 0.2783], device='cuda:0')                                                     rank 1 after allreduce max with FLAGCX_GROUP1: y = tensor([0.9107, 0.2783], device='cuda:1')                                                        rank 0 after allreduce sum with FLAGCX_GROUP1: x = tensor([0.7538, 0.7773], device='cuda:0')                                                                rank 1 after allreduce sum with FLAGCX_GROUP1: x = tensor([0.7538, 0.7773], device='cuda:1') |
|                  |        |                                                              |

## 3.N卡FlagScale + FlagCX LLaMA3-8B训练

### （1）环境准备

- 选择镜像`flagscale_xlc:cuda12.4.1-cudnn9.5.0-python3.12-torch2.6.0-time2504161115-ssh`创建容器名为flagscale-flagcx-test，然后进入容器。

  ```Plain
  conda env list  #查看conda环境
  conda activate flagscale-train #进入到flagscale-train这个环境下进行后续操作
  pip install modelscope 
  pip install pandas
  ```

### （2）编译安装FlagCX库

- 拉取FlagScale和FlagCX代码

  ```Plain
  git clone https://github.com/FlagOpen/FlagScale.git 
  git clone https://github.com/FlagOpen/FlagCX.git
  ```

- FlagCX编译安装

  ```Plain
  cd FlagCX ##查看Makefile文件，可以根据当前运行平台进行编译选项选择，例如USE_NVIDIA、USE_KUNLUNXIN、...，
  make USE_NVIDIA=1  
  cd plugin/torch 
  python setup.py develop --adaptor nvidia
  pip list |grep flagcx ##查看FlagCX编译安装成功后对应的文件绝对路径
  ```

- 正确的编译安装截图

![img](https://jwolpxeehx.feishu.cn/space/api/box/stream/download/asynccode/?code=NTdjOGFlNzVhMmE0Yzk1M2U3NWRlN2NhZTQwMTljNGFfYnV1NVJoYXNDZm1NTWE0S2paTm5zdUVXaE95ZUdnODZfVG9rZW46TmtvN2JtelZsb0ttMkN4N3JST2N4TGhibjhiXzE3NTYyNzcwODI6MTc1NjI4MDY4Ml9WNA)

### （3）数据准备和模型参数配置

- 数据准备

  ```Plain
  cd FlagScale
  mkdir data
  ##提供了来自pile数据集的一小段处理后的数据（bin和idx）:pile_wikipedia_demo，拷贝到FlagScale/data目录下
  ```

- 模型参数配置1

  ```Plain
  cd FlagScale/examples/llama3/conf/ ##路径下包含train、train.yaml、train_hetero.yaml，同构配置train.yaml，异构配置train_hetero.yaml
  vi train.yaml
  -----------------train.yaml配置文件说明---------------------------------
  ##文件包含defaults、experiment、action、hydra四部分，主要是修改defaults和experiment
  ##修改defaults：train: XXXX，XXXX修改为8b
  ##experiment:exp_dir: ./outputs_llama3_8b表示为分布式训练输出文件
  ##修改experiment下runner中的hostfile和envs，因为同构测试，也就是单节点，所以将hostfile这一行注释掉，只有在异构下才进行配置。env中需要设置使用卡编号CUDA_VISIBLE_DEVICES: 0,1,2,3,4,5,6,7
  ```

- 模型参数配置2

  ```Plain
  cd FlagScale/examples/llama3/conf/train #文件下对应得多个不同数据规模模型配置文件xxx.yaml
  vi 8b.yaml 
  ------------------8b.yaml配置文件说明-------------------------------------
  ##文件包含三部分：system、model、data
  ##在system部分添加distributed_backend: flagcx
  ##在model部分参数配置中，可以通过train_samples和global_batch_size进行设置step=train_samples/global_batch_size，一般设置为整数
  ##在data部分参数配置中需要对data_path和tokenizer_path进行修改。设置data_path，设置为上一步数据准备中的data目录下数据cache那一层。tokenizer_path这个需要进行去模型对应的官网上下载。
  ```

- 对应模型的tokenizer下载，链接https://www.modelscope.cn/models/LLM-Research/Meta-Llama-3-8B-Instruct/files，选择通过命令行下载。

  ```Plain
  cd FlagScale/examples/llama3 #将tokenizer下载到当前目录下
  modelscope download --model LLM-Research/Meta-Llama-3-8B-Instruct [XXXX] --local_dir ./
  -------------------------说明----------------------------
  [XXXX]:表示为Meta-Llama-3-8B-Instruct对应的tokenizer文件，例如tokenizer.json、tokenizer_config.json、config.json、configuration.json、generation_config.json
  ```

### （4）unpatch训练后端代码适配

```Plain
cd FlagScale
python tools/patch/unpatch.py --backend Megatron-LM
```

### （5）分布式训练

```Plain
cd FlagScale
python run.py --config-path ./examples/llama3/conf --config-name train action=run ##启动分布式训练
python run.py --config-path ./examples/llama3/conf --config-name train action=stop ##停止分布式训练
```

- 启动分布式训练后，会将配置信息打印出来，并生成了运行脚本/workspace/wuh-test/flagscale_vmain_nv/outputs_llama3_8b/logs/scripts/host_0_localhost_run.sh。能够看到训练结果文件在/workspace/wuh-test/flagscale_vmain_nv/outputs_llama3_8b里面。

  ![img](https://jwolpxeehx.feishu.cn/space/api/box/stream/download/asynccode/?code=ZWFiYWZiMWE4ZDc3MDQ5ZDNkNWMzYWUxZGMyMzZkODVfODR4M05EczdVR0NVdmU5Wm1lN3AwMHNTdVpEVUhaZnNfVG9rZW46R1V2VmJ3STFVb0JMRFV4WVNheWNOb1ZlbkVjXzE3NTYyNzczMjc6MTc1NjI4MDkyN19WNA)

# 四、使用FlagCX进行异构测试

## 1.通信API测试

### （1）环境准备

```Plain
## 假设有两台主机root@<ip1>、root@<ip2>，实现互相免密登陆
ssh-keygen -t rsa  # 一路回车即可
cd /root/.ssh # 进入.ssh目录
cat id_rsa.pub >> authorized_keys # 将公钥导入至authorized_keys
chmod 700 ~/.ssh  # 修改文件权限
chmod 600 authorized_keys # 修改文件权限
-------------------------------------------------------------------------------------------
##进入主机1，想要主机1上免密登录连接主机2
cat /root/.ssh/id_rsa.pub #查看主机1上的公钥然后复制
vi /root/.ssh/authorized_keys  #进入主机2，将主机1的公钥放进去
vi /etc/ssh/sshd_config #编辑 SSH 服务器配置文件，主机1、2都需要进行相同配置
    Port 8010 #端口号
    PermitRootLogin yes
    PubkeyAuthentication yes
    AuthorizedKeysFile   /root/.ssh/authorized_keys
    PasswordAuthentication no
:wq #保存退出编辑
service ssh restart #重启 SSH 服务
ssh -p <端口号> root@<ip2> #如果能够免密登录成功说明配置生效了 
-------------------------------------------------------------------------------------------
##进入主机2，想要主机2上免密登录连接主机1，步骤如上，只是进入主机1，将主机2的公钥放进去1的vi /root/.ssh/authorized_keys
```

### （2）创建软链接

```Plain
cd /root 
ln -s /workspace/flagcx_test_[xxx]/FlagCX ./FlagCX 
---------------说明-------------------------------
/workspace/ #为共享文件夹，主机1、主机2通过容器挂载后，均能够在各自容器中进行访问
flagcx_test_[xxx]/FlagCX #xxx表示不同平台上的FlagCX通信库文件
./FlagCX  #软链接访问文件名，通过/root/FlagCX能够访问到当前平台上的FlagCX库文件
```

### （3）mpich安装确认

```Plain
cd /workspace/mpich-4.2.3   #查看mpich是否安装
```

### （4）Makefile和环境变量配置

```Plain
cd /root/FlagCX/test/perf #进入到通信API测试文件
vi Makefile #进入到Makefile
    MPI_HOME ?= /workspace/mpich-4.2.3/build/ #将mpi路径修改为（3）中的使用的，与（3）中的mpi保持一致
:q #保存推出
export LD_LIBRARY_PATH=/workspace/mpich-4.2.3/build/lib:$LD_LIBRARY_PATH #配置环境变量
```

### （5）异构通信API测试

- 保证主机1、主机2、...，都能按照上述进行配置后，在各自平台上正确运行同构通信API测试。

- 确认主机1、主机2、...的端口为<xxx>，并且保持一致

- 在主机1上运行异构通信API测试脚本前，需要配置端口号环境export HYDRA_LAUNCHER_EXTRA_ARGS="-p 8010"，8010是根据ssh免密登录时配置更改的。

- 在主机1上运行异构通信API测试脚run.sh

  ```Plain
  ##Nvida A800*1 + Muxi C550*1
  /workspace/mpich-4.2.3/build/bin/mpirun \
    -np 2 -hosts 10.1.15.233:1,10.1.15.67:1 \
    -env PATH=/workspace/mpich-4.2.3/build/bin \
    -env LD_LIBRARY_PATH=/workspace/mpich-4.2.3/build/lib:/root/FlagCX-wuh/build/lib:/usr/local/mpi/lib/:/opt/maca/ompi/lib \
    -env FORCE_ACTIVE_WAIT=2 \
    -env FLAGCX_IB_HCA=mlx5 \
    -env FLAGCX_ENABLE_TOPO_DETECT=TRUE \
    -env FLAGCX_DEBUG=INFO \
    -env FLAGCX_DEBUG_SUBSYS=INIT \
    -env CUDA_VISIBLE_DEVICES=1 \
    -env MACA_VISIBLE_DEVICES=3 \
    /root/FlagCX-wuh/test/perf/test_allreduce -b 128K -e 4G -f 2 -w 5 -n 100 -p 1
  ```

- 注意：在异构通信API测试使用两张卡时，出现一些提示表示每个节点只开了 1 张卡，FlagCX 认为没必要走 GPU-C2C allreduce，就退回到 Host 通信了。监测GPU使用率确实为0，allruduce总体运行时间特别长，但是计算结果是正确的。属于正常现象，可以使用2+2的4卡使用GPU进行异构测试。

  ![img](https://jwolpxeehx.feishu.cn/space/api/box/stream/download/asynccode/?code=N2U0Mjc0NDIyYWU1OWQwMDJiYmU1ZjU5NmE2N2NjOTZfeVF0b0FJcXRLbEw2SW0zdWRiRW14bEdoOHRKN25KQ2xfVG9rZW46VUtnMmJVN3pQbzM4SGZ4VThzQmNrSUF2bllnXzE3NTYyNzc2Mjc6MTc1NjI4MTIyN19WNA)

### （6）测试结果

| 测试机器                         | 通信库 | 通信带宽（GB/s）                                             |
| -------------------------------- | ------ | ------------------------------------------------------------ |
|                                  |        | AllReduce                                                    |
| Nvidia A800 * 2 + Metax C550 * 2 | FlagCX | Comm size: 131072 bytes; Elapsed time: 0.008623 sec; Algo bandwidth: 0.015200 GB/s; Bus bandwidth: 0.022800 GB/s                    Comm size: 262144 bytes; Elapsed time: 0.010125 sec; Algo bandwidth: 0.025891 GB/s; Bus bandwidth: 0.038836 GB/s                                 Comm size: 524288 bytes; Elapsed time: 0.009634 sec; Algo bandwidth: 0.054418 GB/s; Bus bandwidth: 0.081627 GB/s                         Comm size: 1048576 bytes; Elapsed time: 0.009137 sec; Algo bandwidth: 0.114764 GB/s; Bus bandwidth: 0.172146 GB/s                        Comm size: 2097152 bytes; Elapsed time: 0.009613 sec; Algo bandwidth: 0.218167 GB/s; Bus bandwidth: 0.327251 GB/s                   Comm size: 4194304 bytes; Elapsed time: 0.012051 sec; Algo bandwidth: 0.348057 GB/s; Bus bandwidth: 0.522086 GB/s                   Comm size: 8388608 bytes; Elapsed time: 0.010939 sec; Algo bandwidth: 0.766843 GB/s; Bus bandwidth: 1.150265 GB/s                 Comm size: 16777216 bytes; Elapsed time: 0.015018 sec; Algo bandwidth: 1.117159 GB/s; Bus bandwidth: 1.675738 GB/s               Comm size: 33554432 bytes; Elapsed time: 0.014919 sec; Algo bandwidth: 2.249136 GB/s; Bus bandwidth: 3.373704 GB/s             Comm size: 67108864 bytes; Elapsed time: 0.015826 sec; Algo bandwidth: 4.240359 GB/s; Bus bandwidth: 6.360538 GB/s                 Comm size: 134217728 bytes; Elapsed time: 0.019956 sec; Algo bandwidth: 6.725734 GB/s; Bus bandwidth: 10.088601 GB/s          Comm size: 268435456 bytes; Elapsed time: 0.030975 sec; Algo bandwidth: 8.666267 GB/s; Bus bandwidth: 12.999401 GB/s Comm size: 536870912 bytes; Elapsed time: 0.051275 sec; Algo bandwidth: 10.470417 GB/s; Bus bandwidth: 15.705625 GB/s Comm size: 1073741824 bytes; Elapsed time: 0.082816 sec; Algo bandwidth: 12.965361 GB/s; Bus bandwidth: 19.448042 GB/s Comm size: 2147483648 bytes; Elapsed time: 0.142399 sec; Algo bandwidth: 15.080749 GB/s; Bus bandwidth: 22.621124 GB/s Comm size: 4294967296 bytes; Elapsed time: 0.268710 sec; Algo bandwidth: 15.983634 GB/s; Bus bandwidth: 23.975451 GB/s |
|                                  |        |                                                              |

