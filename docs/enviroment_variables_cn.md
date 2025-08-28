# FLAGCX_XXX环境变量

|      | Environment Variable Name             | Description                                                  |
| ---- | ------------------------------------- | ------------------------------------------------------------ |
| 1    | export FLAGCX_IB_HCA=mlx5             | `FLAGCX_IB_HCA` specifies the type of InfiniBand HCA (Host Channel Adapter) device to use. `mlx5` indicates using a Mellanox ConnectX-5 network adapter (mlx5 driver) for RDMA communication. |
| 2    | export FLAGCX_ENABLE_TOPO_DETECT=TRUE | `FLAGCX_ENABLE_TOPO_DETECT` specifies whether topology detection is enabled. FlagCX will probe the network topology, bandwidth, and latency between nodes during initialization in order to optimize communication scheduling. `TRUE` — enable; `FALSE` — disable. |
| 3    | export FLAGCX_DEBUG=TRUE              | `FLAGCX_DEBUG` specifies whether debug mode is enabled. `TRUE` — enable debugging; `FALSE`—disable debugging. |
| 4    | export FLAGCX_DEBUG_SUBSYS=ALL        | `FLAGCX_DEBUG_SUBSYS` specifies which subsystem(s) to enable debug output for. `ALL` means debug information for all subsystems will be printed (for example: network initialization, communication module, memory management, scheduler, etc.). You can also specify a single subsystem, for example `COMM` (communication layer), `MEMORY` (memory management), or `TOPO` (topology detection). |
| 5    | export FLAGCX_USENET=mlx5_0           | `FLAGCX_USENET` specifies the network device (netdev) to use for communication. `mlx5_0` is a common naming convention for Mellanox mlx5-series adapters and typically refers to the first mlx5 device. |
| 6    | export FLAGCX_USEDEV=1                | `FLAGCX_USEDEV` specifies whether to enable direct device usage / device passthrough / device mode (a boolean switch). `1` or `TRUE`: enable direct use of underlying device (for example enabling the RDMA/verbs path or NIC "device mode"), bypassing or optimizing the kernel TCP stack to achieve lower latency and/or higher bandwidth. `0` or `FALSE`: disable device passthrough and fall back to the regular network path (e.g., TCP over IP). |
| 7    | export FLAGCX_SOCKET_IFNAME=ens102    | `FLAGCX_SOCKET_IFNAME` specifies which network interface FlagCX should bind to and prefer when using socket/TCP-based communication paths. Setting it to `ens102` means that ordinary TCP/UDP socket traffic (non-RDMA/verbs) will attempt to use the `ens102` interface. |
| 8    |                                       |                                                              |
|      |                                       |                                                              |
|      |                                       |                                                              |

