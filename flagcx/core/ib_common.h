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

// Global arrays (declared as extern, defined in adaptor files)
extern struct flagcxIbDev flagcxIbDevs[MAX_IB_DEVS];
extern struct flagcxIbMergedDev flagcxIbMergedDevs[MAX_IB_VDEVS];

#endif // FLAGCX_IB_COMMON_H_
