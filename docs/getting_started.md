# 一、环境配置

- 容器创建参考命令：

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

- 查看并选择匹配当前运行平台的编译选项

```Plain
git clone https://github.com/FlagOpen/FlagCX.git
cd FlagCX 
make USE_NVIDIA=1 -j$(nproc) # NVIDIA GPU 平台
make USE_CAMBRICON=1 -j$(nproc)  # 寒武纪 CAMBRICON 平台
make USE_KUNLUNXIN=1 -j$(nproc) #昆仑芯平台
make USE_NVIDIA=1 USE_CAMBRICON=1 ... -j$(nproc) #多平台混合编译（如同时支持 NVIDIA 和寒武纪等）
--------------说明---------------------
USE_XXX=1：对应硬件平台（XXX 为平台名称）
-j$(nproc)：使用系统所有 CPU 核心并行编译，加速构建过程
```

## 2.正确编译结果

![img](https://jwolpxeehx.feishu.cn/space/api/box/stream/download/asynccode/?code=ZWZkNzFmNWM1NGVjODljMmU1NjZkMTVhZjIzYzMwNDVfbkt0d2VES1dvTEZEM3FCQVpneWsxNVAzNWU2UjhMbEdfVG9rZW46Uk5iR2JnemM5b0N1TlJ4WEd2RWNETFV2bjZkXzE3NTYyNzgxOTQ6MTc1NjI4MTc5NF9WNA)

# 三、使用FlagCX进行同构测试

## 1.通信API测试

### （1）选择编译选项

```Plain
cd test/perf #进入测试目录
make USE_NVIDIA=1 #根据所在硬件平台选择编译选项
```

### （2）正确编译结果

![img](https://jwolpxeehx.feishu.cn/space/api/box/stream/download/asynccode/?code=MTk3MWJkMGMwN2ZhY2VkMTJiYTg5ZTliZmVhYWM5ZDlfaWhoMzdoTWpjRldjYjZrU2RJNUIyZGtQOEtTMExrMTJfVG9rZW46RlZ6a2JqZkFOb0x5ME94RmZrRmNmTWlHbktmXzE3NTYyNzgzODc6MTc1NjI4MTk4N19WNA)

### （3）通信API测试

```Plain
mpirun --allow-run-as-root -np 2 ./test_allreduce -b 128K -e 4G -f 2 -p 1
----------说明---------------
test_allreduce是一个基于 MPI 与 FlagCX 的 AllReduce 性能基准程序：它将每个 MPI 进程绑定到一块 GPU，根据用户指定的最小/最大消息大小和步长对 AllReduce 操作先做热身再进行正式测量，并在每种消息大小下打印平均耗时、带宽估算以及用于正确性检查的缓冲区片段。例如，用 2 个 MPI 进程2张加速卡运行 test_allreduce 时，测试会从 128 KiB 起按 2 倍递增（128 KiB、256 KiB、512 KiB、1 MiB …）直至 4 GiB，分别记录每个消息尺寸下的带宽、延迟与正确性信息。
```

### （4）正确的性能测试截图

![img](https://jwolpxeehx.feishu.cn/space/api/box/stream/download/asynccode/?code=NmU1NGQ1MTI2Y2UyM2M4YTQzYTA5Y2JiNDc5ZDdiYTBfOEVEUHdZWnBwN1E0MkFvMThmTVYzSG5JMHZLNEhTZWFfVG9rZW46U2FIbGJaNVNwb0ExUjl4WkdxdmNBeTlMbnVkXzE3NTYyNzg0MTA6MTc1NjI4MjAxMF9WNA)

## 2.Torch API测试

### （1）环境准备

- 选择镜像 `flagscale_xlc:cuda12.4.1-cudnn9.5.0-python3.12-torch2.6.0-time2504161115-ssh`创建docker容器并进入。

- 查看容器是否安装了Pytorch，如果没有需要进行安装，例如下面安装流程

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

![img](https://jwolpxeehx.feishu.cn/space/api/box/stream/download/asynccode/?code=MDg3MGNlOTFjNmY4OTAyYWM5OTY2MDM0OGU1OTgzNDVfUm5SdFNqdUJqbXY5Rk44amxmODFWR2I4d3k4ajNoSDNfVG9rZW46VjNxUGI0dm93b0VSTTZ4V0ZmV2NSeFNjbm1oXzE3NTYyNzg1NzU6MTc1NjI4MjE3NV9WNA)

### （4）Torch API测试

- 测试用例在编译安装目录下的`cd /FlagCX/plugin/torch/example/`下的example.py
- 测试脚本run.sh，根据当前运行平台进行更改环境变量的名称，以及使用平台硬件设备的编号。
- 输入命令 `. run.sh`

### （5）正确的性能测试截图

![img](https://jwolpxeehx.feishu.cn/space/api/box/stream/download/asynccode/?code=YjZkNDg0N2RhN2I1NGQyMmI4OGNkMDkxMmIyOWQ1YjNfRmhQY09ENnczdnkyZWhqUlZHbEhNdVpwT0R4MXJpS2lfVG9rZW46TDBoeWJhRXh6b1Z2U3J4aVZSNWNPbVJJblJoXzE3NTYyNzg3Nzg6MTc1NjI4MjM3OF9WNA)

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

### （3）数据准备

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
## 假设有两台主机root@<ip1>、root@<ip2>，实现互相免密登陆配置
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

### （3）异构通信API测试

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

