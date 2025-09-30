/*************************************************************************
 * Copyright (c) 2024, FlagCX Inc.
 * All rights reserved.
 *
 * This file contains common InfiniBand structures and constants
 * shared between IBRC and UCX adaptors.
 ************************************************************************/

#ifndef FLAGCX_IB_COMMON_H_
#define FLAGCX_IB_COMMON_H_

#include "flagcx_net.h"
#include "ibvcore.h"
#include <pthread.h>
#include <stdint.h>

// Common constants for IB adaptors
#define MAXNAMESIZE 64
#define MAX_IB_DEVS 32
#define FLAGCX_IB_MAX_DEVS_PER_NIC 2
#define FLAGCX_NET_MAX_DEVS_PER_NIC 4
#define MAX_MERGED_DEV_NAME                                                    \
  (MAXNAMESIZE * FLAGCX_IB_MAX_DEVS_PER_NIC) + FLAGCX_IB_MAX_DEVS_PER_NIC
#define MAX_IB_VDEVS MAX_IB_DEVS * 8

// Common enums and constants
enum flagcxIbProvider {
  IB_PROVIDER_NONE = 0,
  IB_PROVIDER_MLX5 = 1,
  IB_PROVIDER_MLX4 = 2
};

static const char *ibProviderName[]
    __attribute__((unused)) = {"NONE", "MLX5", "MLX4"};

// Common parameter function declarations
extern int64_t flagcxParamIbMergeVfs(void);
extern int64_t flagcxParamIbAdaptiveRouting(void);
extern int64_t flagcxParamIbMergeNics(void);

// Common IB structures
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

struct flagcxIbStats {
  int fatalErrorCount;
};

// Common device properties structure is now defined in flagcx_net.h

// Unified IB device structure (combines features from both adaptors)
struct flagcxIbDev {
  pthread_mutex_t lock;
  int device;
  int ibProvider;
  uint64_t guid;
  struct ibv_port_attr portAttr;
  int portNum;
  int link;
  int speed;
  struct ibv_context *context;
  int pdRefs;
  struct ibv_pd *pd;
  char devName[MAXNAMESIZE];
  char *pciPath;
  int realPort;
  int maxQp;
  struct flagcxIbMrCache mrCache;
  struct flagcxIbStats stats;
  int ar; // ADAPTIVE_ROUTING
  int isSharpDev;
  struct {
    struct {
      int dataDirect;
    } mlx5;
  } capsProvider;
  int dmaBufSupported;
} __attribute__((aligned(64)));

// Unified merged device structure (used by both IBRC and UCX)
// Contains both direct fields (for IBRC) and structured fields (for UCX)
struct flagcxIbMergedDev {
  // Direct fields (used by IBRC)
  int ndevs;
  int devs[FLAGCX_IB_MAX_DEVS_PER_NIC];

  // Structured fields (used by UCX) - now uses flagcx_net.h definition
  flagcxNetVDeviceProps_t vProps;

  // Common fields
  int speed;
  char devName[MAX_MERGED_DEV_NAME];
} __attribute__((aligned(64)));

// Common QP info structure (used by both IBRC and IBUC)
struct flagcxIbQpInfo {
  uint32_t qpn;
  struct ibv_ece ece;
  int ece_supported;
  int devIndex;
};

// Common device info structure (used by both IBRC and IBUC)
struct flagcxIbDevInfo {
  uint32_t lid;
  uint8_t ib_port;
  enum ibv_mtu mtu;
  uint8_t link_layer;
  uint64_t spn;
  uint64_t iid;
  uint32_t fifoRkey;
  union ibv_gid remoteGid;
};

// Common GID info structure (used by both IBRC and IBUC)
struct flagcxIbGidInfo {
  uint8_t link_layer;
  union ibv_gid localGid;
  int32_t localGidIndex;
};

// Common MR handle structure (used by both IBRC and IBUC)
struct flagcxIbMrHandle {
  ibv_mr *mrs[FLAGCX_IB_MAX_DEVS_PER_NIC];
};

// Common request type constants (used by both IBRC and IBUC)
#define FLAGCX_NET_IB_REQ_UNUSED 0
#define FLAGCX_NET_IB_REQ_SEND 1
#define FLAGCX_NET_IB_REQ_RECV 2
#define FLAGCX_NET_IB_REQ_FLUSH 3

// Common request type string array (used by both IBRC and IBUC)
extern const char *reqTypeStr[];

// Global arrays (declared as extern, defined in adaptor files)
extern struct flagcxIbDev flagcxIbDevs[MAX_IB_DEVS];
extern struct flagcxIbMergedDev flagcxIbMergedDevs[MAX_IB_VDEVS];

#endif // FLAGCX_IB_COMMON_H_
