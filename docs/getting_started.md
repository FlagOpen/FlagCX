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

### （3）安装编译

分别在两台主机上，各自进行FlagCX通信API编译安装，参考上述"三、使用FlagCX进行同构测试"
