/*************************************************************************
 * SPDX-FileCopyrightText: NVIDIA CORPORATION & AFFILIATES
 * Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifndef FLAGCX_UCX_UCT_LIB_H_
#define FLAGCX_UCX_UCT_LIB_H_

#include <assert.h>
#include <stdint.h>
#include <unistd.h>

#include "p2p_plugin.h"
#include "socket.h"

// Forward declarations for functions that were in p2p_plugin.cc
// These are needed when p2p_plugin.cc is not compiled
flagcxResult_t flagcx_p2p_ib_get_properties(flagcxIbDev *devs, int flagcxNMergedIbDevs, int dev, flagcxNetProperties_v8_t* props);
flagcxResult_t flagcx_p2p_ib_init(int *nDevs, int *nmDevs, flagcxIbDev *flagcxIbDevs, char *flagcxIbIfName, union flagcxSocketAddress *flagcxIbIfAddr, pthread_t *flagcxIbAsyncThread, flagcxDebugLogger_t logFunction);
flagcxResult_t flagcx_p2p_dmabuf_support(int dev);
flagcxResult_t flagcx_p2p_gdr_support();

#include <uct/api/uct.h>

#define FLAGCX_UCX_UCT_MAX_RECVS       FLAGCX_NET_IB_MAX_RECVS
#define FLAGCX_UCT_LISTEN_HANDLE_MAGIC 0x43cf19ed91abdb85
#define FLAGCX_UCT_REG_ALIGN           4096

typedef enum {
  FLAGCX_UCT_AM_RTR = 14, /* Use particular values */
  FLAGCX_UCT_AM_ATP = 15,
  FLAGCX_UCT_AM_RTS = 16,
  FLAGCX_UCT_AM_ATS = 17
} flagcx_uct_am_type_t;

typedef enum {
  FLAGCX_UCT_START = 0,
  FLAGCX_UCT_CONNECT,
  FLAGCX_UCT_ACCEPT,
  FLAGCX_UCT_RECEIVE_REMOTE, /* Acceptor receives ep addr/remote communicator */
  FLAGCX_UCT_RECEIVE_ADDR,
  FLAGCX_UCT_RX_READY,
  FLAGCX_UCT_DONE
} flagcx_uct_state_t;

/* UCT EP address to exchange and connect to */
typedef struct {
  uint8_t dev_addr_size;
  uint8_t ep_addr_size;
  uint8_t data[64];
} flagcx_uct_ep_addr_t;

typedef struct {
  uct_iface_h     iface;
  uct_md_h        md;
  uct_component_h comp;
  void            *addr;
  size_t          addr_size;
  void            *dev_addr;
  size_t          dev_addr_size;
  size_t          ep_addr_size;
  size_t          rkey_packed_size;

  size_t          am_max_short;
  size_t          min_get_zcopy;
} flagcx_uct_iface_t;

struct flagcx_uct_context;

typedef struct flagcx_uct_worker {
  struct flagcx_uct_worker *next;
  struct {
    pthread_t thread;
    int       dev;
  } id;

  int                     count;
  ucs_async_context_t     *async;
  uct_worker_h            worker;
  flagcx_uct_iface_t        *uct_iface;
  struct flagcx_uct_context *context;
} flagcx_uct_worker_t;

typedef struct {
  uct_ep_h         ep;
  uct_ep_addr_t    *addr;
  size_t           addr_size;
  flagcx_uct_iface_t *uct_iface;
  uint8_t          data[];
} flagcx_uct_ep_t;

/* All the remote addresses for the communicator */
typedef struct flagcx_uct_comm_addr {
  flagcx_uct_ep_addr_t rma;
  /* TODO: Add multi-QP here */
} flagcx_uct_comm_addr_t;

/* Either Receiver or Sender communicator, connected to one peer */
typedef struct flagcx_uct_comm {
  struct flagcxSocket       sock;
  struct flagcx_uct_context *context;
  int                     dev;

  flagcx_uct_worker_t       *uct_worker;
  flagcx_uct_iface_t        *uct_iface;
  flagcx_uct_ep_t           *uct_ep;

  struct {
    flagcx_uct_comm_addr_t       addr;  /* Remote addresses */
    const struct flagcx_uct_comm *comm; /* Cookie received in connect */
  } remote;

  /* Local GET on current device */
  struct {
    int                enabled;
    flagcx_uct_ep_t      *uct_ep; /* Locally read from HCA */
    flagcx_uct_ep_addr_t addr;

    uint8_t            *mem; /* Dummy memory to read into */
    uct_mem_h          memh;
  } gpu_flush;
} flagcx_uct_comm_t;

/* State tracking used while connecting/accepting only */
typedef struct {
  flagcx_uct_state_t state;
  flagcx_uct_comm_t  *comm;  /* current communicator being created */
  int              offset; /* for Socket reading */
  int              ready;  /* accept must complete after connect */
} flagcx_uct_stage_t;

/* Memory registration handle in flagcx UCT plugin returned by ->regMR() */
typedef struct {
  uct_mem_h         memh;
  flagcx_uct_comm_t   *comm;
  uct_rkey_bundle_t bundle;
  uint8_t           rkey[];
} flagcx_uct_memh_t;

/* On-the-wire handle passed OOB by flagcx from listener to connector */
typedef struct {
  uint64_t                  magic;
  struct {
    union flagcxSocketAddress addr;
    uint32_t                id;
  } listener;
  flagcx_uct_comm_t           *comm; /* Created communicator in accept */
  flagcx_uct_stage_t          stage; /* Used by connector */
} flagcx_uct_listen_handle_t;

/* Communicator while listening to remote ranks */
typedef struct {
  struct flagcxSocket       sock;
  struct flagcx_uct_context *context;
  int                     dev;
  uint32_t                id;
  flagcx_uct_worker_t       *uct_worker;
  flagcx_uct_comm_t         *comm;

  /* Used by acceptor */
  flagcx_uct_stage_t        stage;
} flagcx_uct_listen_comm_t;

/* Global state of the plugin */
typedef struct flagcx_uct_context {
  /* Transport to use */
  const char              *tl_name;

  /* IB devices available */
  int                     dev_count;
  int                     merge_dev_count;

  /* Use by common code to setup communicators */
  struct flagcx_uct_ops {
    flagcxResult_t (*comm_alloc)(flagcx_uct_comm_t **comm);
    flagcxResult_t (*comm_init)(flagcx_uct_comm_t *comm,
                              struct flagcx_uct_context *context,
                              flagcx_uct_worker_t *worker, int dev,
                              const flagcx_uct_comm_t *remote_comm);
    flagcxResult_t (*iface_set)(flagcx_uct_iface_t *uct_iface);
  } ops;

  /* Max sizes needed */
  size_t                  am_short_size;
  size_t                  rkey_size;

  /* OOB socket for accepting/connecting */
  char                    if_name[MAX_IF_NAME_SIZE];
  union flagcxSocketAddress if_addr;

  /* Number of listener created */
  uint32_t                listener_count;

  /* List of created workers */
  flagcx_uct_worker_t       *worker_list;
} flagcx_uct_context_t;

#define UCXCHECK(statement, failure_action, message, ...) \
  do { \
    ucs_status_t _status = statement; \
    if (_status != UCS_OK) { \
      WARN("Failed: " message ": %s", ##__VA_ARGS__, \
           ucs_status_string(_status)); \
      failure_action; \
    } \
  } while (0)

extern flagcx_uct_context_t context;

/* Library functions */
flagcxResult_t flagcx_uct_iface_set_handler(flagcx_uct_iface_t *uct_iface, int id,
                                        uct_am_callback_t callback);
flagcxResult_t flagcx_uct_devices(int *ndev);
flagcxResult_t flagcx_uct_comm_init(flagcx_uct_comm_t *comm,
                                flagcx_uct_context_t *context,
                                flagcx_uct_worker_t *worker, int dev,
                                const flagcx_uct_comm_t *remote_comm);
void flagcx_uct_comm_deinit(flagcx_uct_comm_t *comm);
int flagcx_uct_flush_index(flagcx_uct_comm_t *base, int *sizes, int n);
flagcxResult_t flagcx_uct_flush(flagcx_uct_comm_t *base_comm, void *data, int size,
                            flagcx_uct_memh_t *uct_memh,
                            uct_completion_t *completion, void **request);
void flagcx_uct_empty_callback(uct_completion_t *comp);

/* flagcx common plugin callbacks */
flagcxResult_t flagcx_uct_listen(int dev, void *listen_handle, void **listen_comm);
flagcxResult_t flagcx_uct_accept(void *listen_comm, void **recv_comm,
                             flagcxNetDeviceHandle_v7_t **recvDevComm);
flagcxResult_t flagcx_uct_connect(int dev, flagcxNetCommConfig_t* config, void *listen_handle, void **send_comm,
                              flagcxNetDeviceHandle_t **sendDevComm);
flagcxResult_t flagcx_uct_close_listen(void *listen_comm);
flagcxResult_t flagcx_uct_reg_mr_dmabuf(void *reg_comm, void *data, size_t size,
                                    int type, uint64_t offset, int fd,
                                    void **mhandle);
flagcxResult_t flagcx_uct_reg_mr(void *reg_comm, void *data, size_t size, int type,
                             void **mhandle);
flagcxResult_t flagcx_uct_dereg_mr(void *dereg_comm, void *mhandle);

/* Compatibility callback */
flagcxResult_t flagcx_uct_get_properties_v8(int dev,
                                        flagcxNetProperties_v8_t *props_v8);
flagcxResult_t flagcx_uct_get_properties_v7(int dev,
                                        flagcxNetProperties_v7_t *props_v7);
flagcxResult_t flagcx_uct_reg_mr_v7(void *comm, void *data, int size, int type,
                                void **mhandle);
flagcxResult_t flagcx_uct_get_properties_v6(int dev,
                                        flagcxNetProperties_v6_t *props_v6);
flagcxResult_t flagcx_uct_connect_v6(int dev, void *handle, void **send_comm);
flagcxResult_t flagcx_uct_connect_v8(int dev, void *handle, void **send_comm,
                                    flagcxNetDeviceHandle_v8_t **sendDevComm);
flagcxResult_t flagcx_uct_connect_v7(int dev, void *handle, void **send_comm,
                                    flagcxNetDeviceHandle_v7_t **sendDevComm);
flagcxResult_t flagcx_uct_accept_v6(void *listen_comm, void **recv_comm);
flagcxResult_t flagcx_uct_get_properties(int dev, flagcxNetProperties_t *props);


#define FLAGCX_UCT_PLUGIN_BASE(plugin_name, prefix, init_func, get_properties_func, \
                             connect_func, accept_func, reg_mr_func, \
                             isend_func, irecv_func) \
  { \
    .name          = plugin_name, \
    .init          = prefix##_##init_func, \
    .devices       = flagcx_uct_devices, \
    .getProperties = get_properties_func, \
    .listen        = flagcx_uct_listen, \
    .connect       = connect_func, \
    .accept        = accept_func, \
    .regMr         = reg_mr_func, \
    .regMrDmaBuf   = flagcx_uct_reg_mr_dmabuf, \
    .deregMr       = flagcx_uct_dereg_mr, \
    .isend         = prefix##_##isend_func, \
    .irecv         = prefix##_##irecv_func, \
    .iflush        = prefix##_iflush, \
    .test          = prefix##_test, \
    .closeSend     = prefix##_close, \
    .closeRecv     = prefix##_close, \
    .closeListen   = flagcx_uct_close_listen \
  }


#define FLAGCX_UCT_PLUGIN_V8(plugin_name, prefix) \
  FLAGCX_UCT_PLUGIN_BASE(plugin_name, prefix, init_v8, flagcx_uct_get_properties_v8, \
                       flagcx_uct_connect_v8, flagcx_uct_accept, flagcx_uct_reg_mr, \
                       isend_v8, irecv_v8)

#define FLAGCX_UCT_PLUGIN_V7(plugin_name, prefix) \
  FLAGCX_UCT_PLUGIN_BASE(plugin_name, prefix, init_v7, flagcx_uct_get_properties_v7, \
                       flagcx_uct_connect_v7, flagcx_uct_accept, flagcx_uct_reg_mr_v7, \
                       isend_v8, irecv_v8)

#define FLAGCX_UCT_PLUGIN_V6(plugin_name, prefix) \
  FLAGCX_UCT_PLUGIN_BASE(plugin_name, prefix, init_v6,  flagcx_uct_get_properties_v6, \
                       flagcx_uct_connect_v6, flagcx_uct_accept_v6, \
                       flagcx_uct_reg_mr_v7, isend_v8, irecv_v8)

#define FLAGCX_UCT_PLUGIN_V5(plugin_name, prefix) \
  { \
    .name          = plugin_name, \
    .init          = prefix##_##init_v5, \
    .devices       = flagcx_uct_devices, \
    .getProperties = flagcx_uct_get_properties_v6, \
    .listen        = flagcx_uct_listen, \
    .connect       = flagcx_uct_connect_v6, \
    .accept        = flagcx_uct_accept_v6, \
    .regMr         = flagcx_uct_reg_mr_v7, \
    .deregMr       = flagcx_uct_dereg_mr, \
    .isend         = prefix##_##isend_v8, \
    .irecv         = prefix##_##irecv_v8, \
    .iflush        = prefix##_##iflush, \
    .test          = prefix##_##test, \
    .closeSend     = prefix##_##close, \
    .closeRecv     = prefix##_##close, \
    .closeListen   = flagcx_uct_close_listen \
  }

#endif /* FLAGCX_UCX_UCT_LIB_H_ */
