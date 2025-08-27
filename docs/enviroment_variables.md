# 一、FlagCX安装编译



- 进入项目目录

```Plain
cd FlagCX  
cat Makefile
```

```Plain
DEVICE_HOME = XXX #查看当前运行设备驱动对应的路径
MPI_HOME = XXX  #查看当前环境下的mpi路径
DEVICE_LIB = $(DEVICE_HOME)/lib64  # 查看当前环境下lib库，有些国产平台上是lib不是lib64
DEVICE_INCLUDE = $(DEVICE_HOME)/include #查看当前环境下的include库路径
```

# 二、FlagCX同构测试

1.通信API测试

```Plain
echo 'export MPI_HOME=/usr/local/mpi' >> ~/.bashrc
echo 'export PATH=$MPI_HOME/bin:$PATH' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=$MPI_HOME/lib:$LD_LIBRARY_PATH' >> ~/.bashrc
source ~/.bashrc  # 立即生效环境变量
```