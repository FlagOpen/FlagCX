/*************************************************************************
 * SPDX-FileCopyrightText: NVIDIA CORPORATION & AFFILIATES
 * Copyright (c) 2016-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE.txt for license information
 ************************************************************************/

 #ifndef FLAGCX_P2P_PLUGIN_H_
 #define FLAGCX_P2P_PLUGIN_H_
 
 #include <stdint.h>
 #include <unistd.h>
 #include <assert.h>
 
 #include "flagcx.h"
 #include "net.h"
 #include "ibvwrap.h"
 #include "param.h"
 #include "socket.h"
 #include "utils.h"
 
 #define MAXSUFFIXSIZE 16
 #define MAXNAMESIZE  (64 + MAXSUFFIXSIZE)
 #define FLAGCX_NET_IB_MAX_RECVS 8
 // We need to support FLAGCX_NET_MAX_REQUESTS for each concurrent receive
 #define MAX_REQUESTS (FLAGCX_NET_MAX_REQUESTS*FLAGCX_NET_IB_MAX_RECVS)
 //static_assert(MAX_REQUESTS <= 256, "request id are encoded in wr_id and we need up to 8 requests ids per completion");
 #define IB_DEVICE_SYSFS_FMT "/sys/class/infiniband/%s/device/%s"
 
 #define FLAGCX_IB_LLSTR(ll) (((ll) == IBV_LINK_LAYER_INFINIBAND) ? "IB" : (((ll) == IBV_LINK_LAYER_ETHERNET) ? "RoCE" : "UNSPECIFIED"))
 
 typedef enum flagcx_p2p_plugin {
  FLAGCX_P2P_IB,
  FLAGCX_P2P_UCX_UCT,
  FLAGCX_P2P_LAST
} flagcx_p2p_plugin_t;
 
 struct flagcxIbMr {
   uintptr_t addr;
   size_t pages;
   int refs;
   struct ibv_mr *mr;
 };
 
 struct flagcxIbMrCache {
   struct flagcxIbMr *slots;
   int capacity, population;
 };
 
 #define FLAGCX_IB_MAX_DEVS_PER_NIC 4
 #define MAX_MERGED_DEV_NAME (MAXNAMESIZE*FLAGCX_IB_MAX_DEVS_PER_NIC)+FLAGCX_IB_MAX_DEVS_PER_NIC
 typedef struct flagcxIbMergedDev {
   flagcxNetVDeviceProps_t vProps;
   int speed;
   char devName[MAX_MERGED_DEV_NAME]; 
 } __attribute__((aligned(64))) flagcxIbMergedDev;
 
 struct flagcxIbStats {
   int fatalErrorCount;
 };
 
 struct flagcxIbRequest {
   struct flagcxIbNetCommBase* base;
   int type;
   struct flagcxSocket* sock;
   int events[FLAGCX_IB_MAX_DEVS_PER_NIC];
   struct flagcxIbNetCommDevBase* devBases[FLAGCX_IB_MAX_DEVS_PER_NIC];
   int nreqs;
   union {
     struct {
       int size;
       void* data;
       uint32_t lkeys[FLAGCX_IB_MAX_DEVS_PER_NIC];
       int offset;
     } send;
     struct {
       int* sizes;
     } recv;
   };
 };
 
 // Retain local RoCE address for error logging
 struct flagcxIbGidInfo {
   uint8_t link_layer;
   union ibv_gid localGid;
   int32_t localGidIndex;
 };
 
 typedef struct flagcxIbNetCommDevBase {
   int ibDevN;
   struct ibv_pd* pd;
   struct ibv_cq* cq;
   uint64_t pad[2];
   struct flagcxIbGidInfo gidInfo;
 } flagcxIbNetCommDevBase;
 
 enum flagcxIbProvider {
   IB_PROVIDER_NONE = 0,
   IB_PROVIDER_MLX5 = 1,
   IB_PROVIDER_MAX = 2,
 };
 typedef struct flagcxIbDev {
   pthread_mutex_t lock;
   int      device;
   uint64_t guid;
   uint8_t portNum;
   uint8_t  link;
   uint8_t  isSharpDev;
   int      speed;
   struct   ibv_context* context;
   int      pdRefs;
   struct ibv_pd*  pd;
   char     devName[MAXNAMESIZE];
   char     *pciPath;
   char* virtualPciPath;
   int      realPort;
   int      maxQp;
   float latency;
   struct   flagcxIbMrCache mrCache;
   int ar; // ADAPTIVE_ROUTING
   struct ibv_port_attr portAttr;
   struct flagcxIbStats stats;
   int dmaBufSupported;
   enum flagcxIbProvider ibProvider;
   union {
     struct {
       int dataDirect;
     } mlx5;
   } capsProvider;
 } __attribute__((aligned(64))) flagcxIbDev;
 
 
 #define MAX_IB_DEVS  32
 #define MAX_IB_VDEVS MAX_IB_DEVS*8
 extern struct flagcxIbMergedDev flagcxIbMergedDevs[MAX_IB_VDEVS];
 extern struct flagcxIbDev flagcxIbDevs[MAX_IB_DEVS];
 
 flagcxResult_t flagcx_p2p_gdr_support();
 
 flagcxResult_t flagcx_p2p_dmabuf_support(int dev);
 
 flagcxResult_t flagcx_p2p_ib_pci_path(flagcxIbDev *devs, int num_devs, char* dev_name, char** path, int* real_port);
 
 flagcxResult_t flagcx_p2p_ib_get_properties(flagcxIbDev *devs, int flagcxNMergedIbDevs, int dev, flagcxNetProperties_t* props);
 
 flagcxResult_t flagcx_p2p_ib_init(int *nDevs, int *nmDevs, flagcxIbDev *flagcxIbDevs, char *flagcxIbIfName, union flagcxSocketAddress *flagcxIbIfAddr,
                               pthread_t *flagcxIbAsyncThread, flagcxDebugLogger_t logFunction);
 
 /* Convert value returtned by ibv_query_port to actual link width */
 int flagcx_p2p_ib_width(int width);
 
 /* Convert value returtned by ibv_query_port to actual link speed */
 int flagcx_p2p_ib_speed(int speed);
 
 int64_t flagcxParamSharpMaxComms();
 
 int64_t flagcxParamIbMergeVfs();
 
 int64_t flagcxParamIbMergeNics();
 
 int flagcxIbRelaxedOrderingCapable(void);
 
 flagcx_p2p_plugin_t flagcx_p2p_get_plugin_type();
 
 flagcxResult_t flagcxIbStatsInit(struct flagcxIbStats* stat);
 
 flagcxResult_t flagcxIbMakeVDeviceInternal(int* d, flagcxNetVDeviceProps_t* props, int nDevs, int *nmDevs);
 
 #endif
 