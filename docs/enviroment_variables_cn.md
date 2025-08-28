# FLAGCX_XXX环境变量

|      | 环境变量名                            | 描述                                                         |
| ---- | ------------------------------------- | ------------------------------------------------------------ |
| 1    | export FLAGCX_IB_HCA=mlx5             | `FLAGCX_IB_HCA`指定使用的 InfiniBand HCA (Host Channel Adapter) 设备类型。`mlx5`  表示使用 Mellanox ConnectX-5 (mlx5 驱动) 网卡进行 RDMA 通信。 |
| 2    | export FLAGCX_ENABLE_TOPO_DETECT=TRUE | `FLAGCX_ENABLE_TOPO_DETECT`是否启用拓扑检测，也就是 FlagCX 会在初始化时探测节点之间的网络拓扑、带宽、延迟，从而做通信调度优化。`TRUE`--开启；`FALSE` --不启用` |
| 3    | export FLAGCX_DEBUG=TRUE              | `FLAGCX_DEBUG` 是否打卡调试模式。`TRUE` --开启调试，`FALSE`-- 关闭调试。 |
| 4    | export FLAGCX_DEBUG_SUBSYS=ALL        | `FLAGCX_DEBUG_SUBSYS`指定要调试的子系统。``ALL`表示所有子系统的调试信息都输出，比如网络初始化、通信模块、内存管理、调度器等。也可以是单独指定子系统，例如：通信层（`COMM`）、内存管理（`MEMORY`）、拓扑检测（`TOPO`）。 |
| 5    | export FLAGCX_USENET=mlx5_0           | `FLAGCX_USENET`表示 选择用于通信的网络设备（netdev）。`mlx5_0` 常见于Mellanox网卡/驱动的命名（表示第一块 mlx5 系列设备）。 |
| 6    | export FLAGCX_USEDEV=1                | `FLAGCX_USEDEV`表示 是否开启“使用设备/设备直通/设备模式”（boolean 开关）。`1` 或 `TRUE`：启用直接使用底层设备（比如启用 RDMA/verbs 路径或 NIC 的“设备模式”），绕过或优化内核 TCP 栈以获得更低延迟/更高带宽。`0` 或 `FALSE`：禁用设备直通，回退到常规网络路径（如 TCP over IP）。 |
| 7    | export FLAGCX_SOCKET_IFNAME=ens102    | `FLAGCX_SOCKET_IFNAME` 表示FlagCX在使用基于 socket/TCP 的通信路径时，应绑定并优先使用哪一个网络接口。把它设为`ens102`，就是说：所有普通 TCP/UDP socket流量（非 RDMA/verbs）将尝试走ens102这个网口。 |
| 8    |                                       |                                                              |
|      |                                       |                                                              |
|      |                                       |                                                              |

