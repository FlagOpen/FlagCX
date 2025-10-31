/*************************************************************************
 * Copyright (c) 2016-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifdef USE_IBUC

#include "adaptor.h"
#include "core.h"
#include "flagcx_common.h"
#include "flagcx_net.h"
#include "ib_common.h"
#include "ibuc_retrans.h"
#include "ibvwrap.h"
#include "net.h"
#include "param.h"
#include "socket.h"
#include "timer.h"
#include "utils.h"
#include <assert.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

flagcxResult_t flagcxIbucInit() {
  flagcxResult_t ret;
  if (flagcxParamIbDisable()) {
    return flagcxInternalError;
  }
  static int shownIbucHcaEnv = 0;
  if (flagcxWrapIbvSymbols() != flagcxSuccess) {
    return flagcxInternalError;
  }

  if (flagcxNIbDevs == -1) {
    pthread_mutex_lock(&flagcxIbLock);
    flagcxWrapIbvForkInit();
    if (flagcxNIbDevs == -1) {
      flagcxNIbDevs = 0;
      flagcxNMergedIbDevs = 0;
      if (flagcxFindInterfaces(flagcxIbIfName, &flagcxIbIfAddr,
                               MAX_IF_NAME_SIZE, 1) != 1) {
        WARN("NET/IBUC : No IP interface found.");
        ret = flagcxInternalError;
        goto fail;
      }

      // Detect IB cards
      int nIbucDevs;
      struct ibv_device **devices;

      // Check if user defined which IBUC device:port to use
      char *userIbucEnv = getenv("FLAGCX_IB_HCA");
      if (userIbucEnv != NULL && shownIbucHcaEnv++ == 0)
        INFO(FLAGCX_NET | FLAGCX_ENV, "FLAGCX_IB_HCA set to %s", userIbucEnv);
      struct netIf userIfs[MAX_IB_DEVS];
      bool searchNot = userIbucEnv && userIbucEnv[0] == '^';
      if (searchNot)
        userIbucEnv++;
      bool searchExact = userIbucEnv && userIbucEnv[0] == '=';
      if (searchExact)
        userIbucEnv++;
      int nUserIfs = parseStringList(userIbucEnv, userIfs, MAX_IB_DEVS);

      if (flagcxSuccess != flagcxWrapIbvGetDeviceList(&devices, &nIbucDevs)) {
        ret = flagcxInternalError;
        goto fail;
      }

      for (int d = 0; d < nIbucDevs && flagcxNIbDevs < MAX_IB_DEVS; d++) {
        struct ibv_context *context;
        if (flagcxSuccess != flagcxWrapIbvOpenDevice(&context, devices[d]) ||
            context == NULL) {
          WARN("NET/IBUC : Unable to open device %s", devices[d]->name);
          continue;
        }
        int nPorts = 0;
        struct ibv_device_attr devAttr;
        memset(&devAttr, 0, sizeof(devAttr));
        if (flagcxSuccess != flagcxWrapIbvQueryDevice(context, &devAttr)) {
          WARN("NET/IBUC : Unable to query device %s", devices[d]->name);
          if (flagcxSuccess != flagcxWrapIbvCloseDevice(context)) {
            ret = flagcxInternalError;
            goto fail;
          }
          continue;
        }
        for (int port_num = 1; port_num <= devAttr.phys_port_cnt; port_num++) {
          struct ibv_port_attr portAttr;
          if (flagcxSuccess !=
              flagcxWrapIbvQueryPort(context, port_num, &portAttr)) {
            WARN("NET/IBUC : Unable to query port_num %d", port_num);
            continue;
          }
          if (portAttr.state != IBV_PORT_ACTIVE)
            continue;
          if (portAttr.link_layer != IBV_LINK_LAYER_INFINIBAND &&
              portAttr.link_layer != IBV_LINK_LAYER_ETHERNET)
            continue;

          // check against user specified HCAs/ports
          if (!(matchIfList(devices[d]->name, port_num, userIfs, nUserIfs,
                            searchExact) ^
                searchNot)) {
            continue;
          }
          pthread_mutex_init(&flagcxIbDevs[flagcxNIbDevs].lock, NULL);
          flagcxIbDevs[flagcxNIbDevs].device = d;
          flagcxIbDevs[flagcxNIbDevs].guid = devAttr.sys_image_guid;
          flagcxIbDevs[flagcxNIbDevs].portAttr = portAttr;
          flagcxIbDevs[flagcxNIbDevs].portNum = port_num;
          flagcxIbDevs[flagcxNIbDevs].link = portAttr.link_layer;
          flagcxIbDevs[flagcxNIbDevs].speed =
              flagcxIbSpeed(portAttr.active_speed) *
              flagcxIbWidth(portAttr.active_width);
          flagcxIbDevs[flagcxNIbDevs].context = context;
          flagcxIbDevs[flagcxNIbDevs].pdRefs = 0;
          flagcxIbDevs[flagcxNIbDevs].pd = NULL;
          strncpy(flagcxIbDevs[flagcxNIbDevs].devName, devices[d]->name,
                  MAXNAMESIZE);
          FLAGCXCHECK(
              flagcxIbGetPciPath(flagcxIbDevs[flagcxNIbDevs].devName,
                                 &flagcxIbDevs[flagcxNIbDevs].pciPath,
                                 &flagcxIbDevs[flagcxNIbDevs].realPort));
          flagcxIbDevs[flagcxNIbDevs].maxQp = devAttr.max_qp;
          flagcxIbDevs[flagcxNIbDevs].mrCache.capacity = 0;
          flagcxIbDevs[flagcxNIbDevs].mrCache.population = 0;
          flagcxIbDevs[flagcxNIbDevs].mrCache.slots = NULL;

          // Enable ADAPTIVE_ROUTING by default on IBUC networks
          // But allow it to be overloaded by an env parameter
          flagcxIbDevs[flagcxNIbDevs].ar =
              (portAttr.link_layer == IBV_LINK_LAYER_INFINIBAND) ? 1 : 0;
          if (flagcxParamIbAdaptiveRouting() != -2)
            flagcxIbDevs[flagcxNIbDevs].ar = flagcxParamIbAdaptiveRouting();

          TRACE(
              FLAGCX_NET,
              "NET/IBUC: [%d] %s:%s:%d/%s speed=%d context=%p pciPath=%s ar=%d",
              d, devices[d]->name, devices[d]->dev_name,
              flagcxIbDevs[flagcxNIbDevs].portNum,
              portAttr.link_layer == IBV_LINK_LAYER_INFINIBAND ? "IB" : "RoCE",
              flagcxIbDevs[flagcxNIbDevs].speed, context,
              flagcxIbDevs[flagcxNIbDevs].pciPath,
              flagcxIbDevs[flagcxNIbDevs].ar);

          pthread_create(&flagcxIbAsyncThread, NULL, flagcxIbAsyncThreadMain,
                         flagcxIbDevs + flagcxNIbDevs);
          flagcxSetThreadName(flagcxIbAsyncThread, "FLAGCX IbucAsync %2d",
                              flagcxNIbDevs);
          pthread_detach(flagcxIbAsyncThread); // will not be pthread_join()'d

          int mergedDev = flagcxNMergedIbDevs;
          if (flagcxParamIbMergeNics()) {
            mergedDev = flagcxIbFindMatchingDev(flagcxNIbDevs);
          }

          // No matching dev found, create new mergedDev entry (it's okay if
          // there's only one dev inside)
          if (mergedDev == flagcxNMergedIbDevs) {
            // Set ndevs to 1, assign first ibDevN to the current IBUC device
            flagcxIbMergedDevs[mergedDev].ndevs = 1;
            flagcxIbMergedDevs[mergedDev].devs[0] = flagcxNIbDevs;
            flagcxNMergedIbDevs++;
            strncpy(flagcxIbMergedDevs[mergedDev].devName,
                    flagcxIbDevs[flagcxNIbDevs].devName, MAXNAMESIZE);
            // Matching dev found, edit name
          } else {
            // Set next device in this array to the current IBUC device
            int ndevs = flagcxIbMergedDevs[mergedDev].ndevs;
            flagcxIbMergedDevs[mergedDev].devs[ndevs] = flagcxNIbDevs;
            flagcxIbMergedDevs[mergedDev].ndevs++;
            snprintf(flagcxIbMergedDevs[mergedDev].devName +
                         strlen(flagcxIbMergedDevs[mergedDev].devName),
                     MAXNAMESIZE + 1, "+%s",
                     flagcxIbDevs[flagcxNIbDevs].devName);
          }

          // Aggregate speed
          flagcxIbMergedDevs[mergedDev].speed +=
              flagcxIbDevs[flagcxNIbDevs].speed;
          flagcxNIbDevs++;
          nPorts++;
        }
        if (nPorts == 0 && flagcxSuccess != flagcxWrapIbvCloseDevice(context)) {
          ret = flagcxInternalError;
          goto fail;
        }
      }
      if (nIbucDevs &&
          (flagcxSuccess != flagcxWrapIbvFreeDeviceList(devices))) {
        ret = flagcxInternalError;
        goto fail;
      };
    }
    if (flagcxNIbDevs == 0) {
      INFO(FLAGCX_INIT | FLAGCX_NET, "NET/IBUC : No device found.");
    } else {
      char line[2048];
      line[0] = '\0';
      // Determine whether RELAXED_ORDERING is enabled and possible
      flagcxIbRelaxedOrderingEnabled = flagcxIbRelaxedOrderingCapable();
      for (int d = 0; d < flagcxNMergedIbDevs; d++) {
        struct flagcxIbMergedDev *mergedDev = flagcxIbMergedDevs + d;
        if (mergedDev->ndevs > 1) {
          // Print out merged dev info
          snprintf(line + strlen(line), 2047 - strlen(line), " [%d]={", d);
          for (int i = 0; i < mergedDev->ndevs; i++) {
            int ibucDev = mergedDev->devs[i];
            snprintf(line + strlen(line), 2047 - strlen(line),
                     "[%d] %s:%d/%s%s", ibucDev, flagcxIbDevs[ibucDev].devName,
                     flagcxIbDevs[ibucDev].portNum,
                     flagcxIbDevs[ibucDev].link == IBV_LINK_LAYER_INFINIBAND
                         ? "IB"
                         : "RoCE",
                     // Insert comma to delineate
                     i == (mergedDev->ndevs - 1) ? "" : ", ");
          }
          snprintf(line + strlen(line), 2047 - strlen(line), "}");
        } else {
          int ibucDev = mergedDev->devs[0];
          snprintf(line + strlen(line), 2047 - strlen(line), " [%d]%s:%d/%s",
                   ibucDev, flagcxIbDevs[ibucDev].devName,
                   flagcxIbDevs[ibucDev].portNum,
                   flagcxIbDevs[ibucDev].link == IBV_LINK_LAYER_INFINIBAND
                       ? "IB"
                       : "RoCE");
        }
      }
      line[2047] = '\0';
      char addrline[SOCKET_NAME_MAXLEN + 1];
      INFO(FLAGCX_NET, "NET/IBUC : Using%s %s; OOB %s:%s", line,
           flagcxIbRelaxedOrderingEnabled ? "[RO]" : "", flagcxIbIfName,
           flagcxSocketToString(&flagcxIbIfAddr, addrline));
    }
    pthread_mutex_unlock(&flagcxIbLock);
  }
  return flagcxSuccess;
fail:
  pthread_mutex_unlock(&flagcxIbLock);
  return ret;
}

flagcxResult_t flagcxIbucMalloc(void **ptr, size_t size);
flagcxResult_t flagcxIbucCreateQpWithType(uint8_t ib_port,
                                          struct flagcxIbNetCommDevBase *base,
                                          int access_flags,
                                          enum ibv_qp_type qp_type,
                                          struct flagcxIbQp *qp);
flagcxResult_t flagcxIbucCreateQpWithTypeSrq(
    uint8_t ib_port, struct flagcxIbNetCommDevBase *base, int access_flags,
    enum ibv_qp_type qp_type, struct flagcxIbQp *qp, struct ibv_srq *srq);
flagcxResult_t flagcxIbucRtrQpWithType(struct ibv_qp *qp, uint8_t sGidIndex,
                                       uint32_t dest_qp_num,
                                       struct flagcxIbDevInfo *info,
                                       enum ibv_qp_type qp_type);
flagcxResult_t flagcxIbucRtsQpWithType(struct ibv_qp *qp,
                                       enum ibv_qp_type qp_type);

flagcxResult_t flagcxIbucMalloc(void **ptr, size_t size) {
  *ptr = malloc(size);
  return (*ptr == NULL) ? flagcxInternalError : flagcxSuccess;
}

static void flagcxIbucAddEvent(struct flagcxIbRequest *req, int devIndex,
                               struct flagcxIbNetCommDevBase *base) {
  req->events[devIndex]++;
  req->devBases[devIndex] = base;
}

flagcxResult_t flagcxIbucInitCommDevBase(int ibDevN,
                                         struct flagcxIbNetCommDevBase *base) {
  base->ibDevN = ibDevN;
  flagcxIbDev *ibucDev = flagcxIbDevs + ibDevN;
  pthread_mutex_lock(&ibucDev->lock);
  if (0 == ibucDev->pdRefs++) {
    flagcxResult_t res;
    FLAGCXCHECKGOTO(flagcxWrapIbvAllocPd(&ibucDev->pd, ibucDev->context), res,
                    failure);
    if (0) {
    failure:
      pthread_mutex_unlock(&ibucDev->lock);
      return res;
    }
  }
  base->pd = ibucDev->pd;
  pthread_mutex_unlock(&ibucDev->lock);

  // Recv requests can generate 2 completions (one for the post FIFO, one for
  // the Recv).
  FLAGCXCHECK(flagcxWrapIbvCreateCq(
      &base->cq, ibucDev->context, 2 * MAX_REQUESTS * flagcxParamIbQpsPerConn(),
      NULL, NULL, 0));

  return flagcxSuccess;
}

flagcxResult_t flagcxIbucDestroyBase(struct flagcxIbNetCommDevBase *base) {
  flagcxResult_t res;
  FLAGCXCHECK(flagcxWrapIbvDestroyCq(base->cq));

  pthread_mutex_lock(&flagcxIbDevs[base->ibDevN].lock);
  if (0 == --flagcxIbDevs[base->ibDevN].pdRefs) {
    FLAGCXCHECKGOTO(flagcxWrapIbvDeallocPd(flagcxIbDevs[base->ibDevN].pd), res,
                    returning);
  }
  res = flagcxSuccess;
returning:
  pthread_mutex_unlock(&flagcxIbDevs[base->ibDevN].lock);
  return res;
}

flagcxResult_t flagcxIbucCreateQp(uint8_t ib_port,
                                  struct flagcxIbNetCommDevBase *base,
                                  int access_flags, struct flagcxIbQp *qp) {
  return flagcxIbucCreateQpWithType(ib_port, base, access_flags, IBV_QPT_UC,
                                    qp);
}

flagcxResult_t flagcxIbucCreateQpWithType(uint8_t ib_port,
                                          struct flagcxIbNetCommDevBase *base,
                                          int access_flags,
                                          enum ibv_qp_type qp_type,
                                          struct flagcxIbQp *qp) {
  return flagcxIbucCreateQpWithTypeSrq(ib_port, base, access_flags, qp_type, qp,
                                       NULL);
}

flagcxResult_t flagcxIbucCreateQpWithTypeSrq(
    uint8_t ib_port, struct flagcxIbNetCommDevBase *base, int access_flags,
    enum ibv_qp_type qp_type, struct flagcxIbQp *qp, struct ibv_srq *srq) {
  struct ibv_qp_init_attr qpInitAttr;
  memset(&qpInitAttr, 0, sizeof(struct ibv_qp_init_attr));
  qpInitAttr.send_cq = base->cq;
  qpInitAttr.recv_cq = base->cq;
  qpInitAttr.qp_type = qp_type;

  if (srq != NULL) {
    qpInitAttr.srq = srq;
    qpInitAttr.cap.max_recv_wr = 0;
    TRACE(FLAGCX_NET, "Creating UC QP with SRQ: srq=%p", srq);
  } else {
    qpInitAttr.cap.max_recv_wr = MAX_REQUESTS;
  }

  // We might send 2 messages per send (RDMA and RDMA_WITH_IMM)
  qpInitAttr.cap.max_send_wr = 2 * MAX_REQUESTS;
  qpInitAttr.cap.max_send_sge = 1;
  qpInitAttr.cap.max_recv_sge = 1;
  qpInitAttr.cap.max_inline_data =
      flagcxParamIbUseInline() ? sizeof(struct flagcxIbSendFifo) : 0;
  FLAGCXCHECK(flagcxWrapIbvCreateQp(&qp->qp, base->pd, &qpInitAttr));

  struct ibv_qp_attr qpAttr;
  memset(&qpAttr, 0, sizeof(struct ibv_qp_attr));
  qpAttr.qp_state = IBV_QPS_INIT;
  qpAttr.pkey_index = flagcxParamIbPkey();
  qpAttr.port_num = ib_port;
  qpAttr.qp_access_flags = access_flags;
  FLAGCXCHECK(flagcxWrapIbvModifyQp(qp->qp, &qpAttr,
                                    IBV_QP_STATE | IBV_QP_PKEY_INDEX |
                                        IBV_QP_PORT | IBV_QP_ACCESS_FLAGS));
  return flagcxSuccess;
}

flagcxResult_t flagcxIbucRtrQp(struct ibv_qp *qp, uint8_t sGidIndex,
                               uint32_t dest_qp_num,
                               struct flagcxIbDevInfo *info) {
  return flagcxIbucRtrQpWithType(qp, sGidIndex, dest_qp_num, info, IBV_QPT_UC);
}

flagcxResult_t flagcxIbucRtrQpWithType(struct ibv_qp *qp, uint8_t sGidIndex,
                                       uint32_t dest_qp_num,
                                       struct flagcxIbDevInfo *info,
                                       enum ibv_qp_type qp_type) {
  struct ibv_qp_attr qpAttr;
  memset(&qpAttr, 0, sizeof(struct ibv_qp_attr));
  qpAttr.qp_state = IBV_QPS_RTR;
  qpAttr.path_mtu = info->mtu;
  qpAttr.dest_qp_num = dest_qp_num;
  qpAttr.rq_psn = 0;

  // For RC mode, we need additional parameters
  if (qp_type == IBV_QPT_RC) {
    qpAttr.max_dest_rd_atomic = 1;
    qpAttr.min_rnr_timer = 12;
  }
  if (info->link_layer == IBV_LINK_LAYER_ETHERNET) {
    qpAttr.ah_attr.is_global = 1;
    qpAttr.ah_attr.grh.dgid.global.subnet_prefix = info->spn;
    qpAttr.ah_attr.grh.dgid.global.interface_id = info->iid;
    qpAttr.ah_attr.grh.flow_label = 0;
    qpAttr.ah_attr.grh.sgid_index = sGidIndex;
    qpAttr.ah_attr.grh.hop_limit = 255;
    qpAttr.ah_attr.grh.traffic_class = flagcxParamIbTc();
  } else {
    qpAttr.ah_attr.is_global = 0;
    qpAttr.ah_attr.dlid = info->lid;
  }
  qpAttr.ah_attr.sl = flagcxParamIbSl();
  qpAttr.ah_attr.src_path_bits = 0;
  qpAttr.ah_attr.port_num = info->ib_port;
  int modify_flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                     IBV_QP_DEST_QPN | IBV_QP_RQ_PSN;
  if (qp_type == IBV_QPT_RC) {
    modify_flags |= IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
  }
  FLAGCXCHECK(flagcxWrapIbvModifyQp(qp, &qpAttr, modify_flags));
  return flagcxSuccess;
}

flagcxResult_t flagcxIbucRtsQp(struct ibv_qp *qp) {
  return flagcxIbucRtsQpWithType(qp, IBV_QPT_UC);
}

flagcxResult_t flagcxIbucRtsQpWithType(struct ibv_qp *qp,
                                       enum ibv_qp_type qp_type) {
  struct ibv_qp_attr qpAttr;
  memset(&qpAttr, 0, sizeof(struct ibv_qp_attr));
  qpAttr.qp_state = IBV_QPS_RTS;
  qpAttr.sq_psn = 0;

  // For RC mode, we need additional parameters
  if (qp_type == IBV_QPT_RC) {
    qpAttr.timeout = flagcxParamIbTimeout();
    qpAttr.retry_cnt = flagcxParamIbRetryCnt();
    qpAttr.rnr_retry = 7;
    qpAttr.max_rd_atomic = 1;
  }

  int modify_flags = IBV_QP_STATE | IBV_QP_SQ_PSN;
  if (qp_type == IBV_QPT_RC) {
    modify_flags |= IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY |
                    IBV_QP_MAX_QP_RD_ATOMIC;
  }
  FLAGCXCHECK(flagcxWrapIbvModifyQp(qp, &qpAttr, modify_flags));
  return flagcxSuccess;
}

flagcxResult_t flagcxIbucListen(int dev, void *opaqueHandle,
                                void **listenComm) {
  struct flagcxIbListenComm *comm;
  FLAGCXCHECK(flagcxCalloc(&comm, 1));
  struct flagcxIbHandle *handle = (struct flagcxIbHandle *)opaqueHandle;
  memset(handle, 0, sizeof(struct flagcxIbHandle));
  comm->dev = dev;
  handle->magic = FLAGCX_SOCKET_MAGIC;
  FLAGCXCHECK(flagcxSocketInit(&comm->sock, &flagcxIbIfAddr, handle->magic,
                               flagcxSocketTypeNetIb, NULL, 1));
  FLAGCXCHECK(flagcxSocketListen(&comm->sock));
  FLAGCXCHECK(flagcxSocketGetAddr(&comm->sock, &handle->connectAddr));
  *listenComm = comm;
  return flagcxSuccess;
}

flagcxResult_t flagcxIbucConnect(int dev, void *opaqueHandle, void **sendComm) {
  struct flagcxIbHandle *handle = (struct flagcxIbHandle *)opaqueHandle;
  struct flagcxIbCommStage *stage = &handle->stage;
  struct flagcxIbSendComm *comm = (struct flagcxIbSendComm *)stage->comm;
  int ready;
  *sendComm = NULL;

  if (stage->state == flagcxIbCommStateConnect)
    goto ibuc_connect_check;
  if (stage->state == flagcxIbCommStateSend)
    goto ibuc_send;
  if (stage->state == flagcxIbCommStateConnecting)
    goto ibuc_connect;
  if (stage->state == flagcxIbCommStateConnected)
    goto ibuc_send_ready;
  if (stage->state != flagcxIbCommStateStart) {
    WARN("Error: trying to connect already connected sendComm");
    return flagcxInternalError;
  }

  FLAGCXCHECK(
      flagcxIbucMalloc((void **)&comm, sizeof(struct flagcxIbSendComm)));
  FLAGCXCHECK(flagcxSocketInit(&comm->base.sock, &handle->connectAddr,
                               handle->magic, flagcxSocketTypeNetIb, NULL, 1));
  stage->comm = comm;
  stage->state = flagcxIbCommStateConnect;
  FLAGCXCHECK(flagcxSocketConnect(&comm->base.sock));

ibuc_connect_check:
  /* since flagcxSocketConnect is async, we must check if connection is complete
   */
  FLAGCXCHECK(flagcxSocketReady(&comm->base.sock, &ready));
  if (!ready)
    return flagcxSuccess;

  // IBUC Setup
  struct flagcxIbMergedDev *mergedDev;
  mergedDev = flagcxIbMergedDevs + dev;
  comm->base.ndevs = mergedDev->ndevs;
  comm->base.nqps = flagcxParamIbQpsPerConn() *
                    comm->base.ndevs; // We must have at least 1 qp per-device
  comm->base.isSend = true;

  // Init PD, Ctx for each IB device
  comm->ar = 1; // Set to 1 for logic
  for (int i = 0; i < mergedDev->ndevs; i++) {
    int ibDevN = mergedDev->devs[i];
    FLAGCXCHECK(flagcxIbucInitCommDevBase(ibDevN, &comm->devs[i].base));
    comm->ar = comm->ar &&
               flagcxIbDevs[dev]
                   .ar; // ADAPTIVE_ROUTING - if all merged devs have it enabled
  }

  struct flagcxIbConnectionMetadata meta;
  meta.ndevs = comm->base.ndevs;

  // Alternate QPs between devices
  int devIndex;
  devIndex = 0;
  for (int q = 0; q < comm->base.nqps; q++) {
    flagcxIbSendCommDev *commDev = comm->devs + devIndex;
    flagcxIbDev *ibucDev = flagcxIbDevs + commDev->base.ibDevN;
    FLAGCXCHECK(flagcxIbucCreateQp(ibucDev->portNum, &commDev->base,
                                   IBV_ACCESS_REMOTE_WRITE,
                                   comm->base.qps + q));
    comm->base.qps[q].devIndex = devIndex;
    meta.qpInfo[q].qpn = comm->base.qps[q].qp->qp_num;
    meta.qpInfo[q].devIndex = comm->base.qps[q].devIndex;

    // Query ece capabilities (enhanced connection establishment)
    FLAGCXCHECK(flagcxWrapIbvQueryEce(comm->base.qps[q].qp, &meta.qpInfo[q].ece,
                                      &meta.qpInfo[q].ece_supported));
    devIndex = (devIndex + 1) % comm->base.ndevs;
  }

  meta.retransEnabled = flagcxParamIbRetransEnable();

  for (int i = 0; i < comm->base.ndevs; i++) {
    flagcxIbSendCommDev *commDev = comm->devs + i;
    flagcxIbDev *ibucDev = flagcxIbDevs + commDev->base.ibDevN;

    // Write to the metadata struct via this pointer
    flagcxIbDevInfo *devInfo = meta.devs + i;
    devInfo->ib_port = ibucDev->portNum;
    devInfo->mtu = ibucDev->portAttr.active_mtu;
    devInfo->lid = ibucDev->portAttr.lid;

    // Prepare my fifo
    FLAGCXCHECK(
        flagcxWrapIbvRegMr(&commDev->fifoMr, commDev->base.pd, comm->fifo,
                           sizeof(struct flagcxIbSendFifo) * MAX_REQUESTS *
                               FLAGCX_NET_IB_MAX_RECVS,
                           IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                               IBV_ACCESS_REMOTE_READ));
    devInfo->fifoRkey = commDev->fifoMr->rkey;

    // RoCE support
    devInfo->link_layer = commDev->base.gidInfo.link_layer =
        ibucDev->portAttr.link_layer;
    if (devInfo->link_layer == IBV_LINK_LAYER_ETHERNET) {
      FLAGCXCHECK(flagcxIbGetGidIndex(ibucDev->context, ibucDev->portNum,
                                      ibucDev->portAttr.gid_tbl_len,
                                      &commDev->base.gidInfo.localGidIndex));
      FLAGCXCHECK(flagcxWrapIbvQueryGid(ibucDev->context, ibucDev->portNum,
                                        commDev->base.gidInfo.localGidIndex,
                                        &commDev->base.gidInfo.localGid));
      devInfo->spn = commDev->base.gidInfo.localGid.global.subnet_prefix;
      devInfo->iid = commDev->base.gidInfo.localGid.global.interface_id;
    } else {
      commDev->base.gidInfo.localGidIndex = 0;
      memset(&commDev->base.gidInfo.localGid, 0, sizeof(union ibv_gid));
    }

    if (meta.retransEnabled) {
      FLAGCXCHECK(flagcxIbCreateCtrlQp(ibucDev->context, commDev->base.pd,
                                       ibucDev->portNum, &commDev->ctrlQp));
      meta.ctrlQpn[i] = commDev->ctrlQp.qp->qp_num;
      meta.ctrlLid[i] = ibucDev->portAttr.lid;
      meta.ctrlGid[i] = commDev->base.gidInfo.localGid;

      size_t ack_buf_size = (sizeof(struct flagcxIbAckMsg) + FLAGCX_IB_ACK_BUF_PADDING) * FLAGCX_IB_ACK_BUF_COUNT;
      commDev->ackBuffer = malloc(ack_buf_size);
      FLAGCXCHECK(flagcxWrapIbvRegMr(&commDev->ackMr, commDev->base.pd,
                                     commDev->ackBuffer, ack_buf_size,
                                     IBV_ACCESS_LOCAL_WRITE));

      TRACE(FLAGCX_NET,
            "Send: Created control QP for dev %d: qpn=%u, link_layer=%d, "
            "lid=%u, gid=%lx:%lx",
            i, commDev->ctrlQp.qp->qp_num, devInfo->link_layer, meta.ctrlLid[i],
            (unsigned long)meta.ctrlGid[i].global.subnet_prefix,
            (unsigned long)meta.ctrlGid[i].global.interface_id);
    }

    if (devInfo->link_layer == IBV_LINK_LAYER_INFINIBAND) { // IB
      for (int q = 0; q < comm->base.nqps; q++) {
        // Print just the QPs for this dev
        if (comm->base.qps[q].devIndex == i)
          INFO(FLAGCX_NET,
               "NET/IBUC: %s %d IbucDev %d Port %d qpn %d mtu %d LID %d "
               "fifoRkey=0x%x fifoLkey=0x%x",
               comm->base.ndevs > 2 ? "FLAGCX MergedDev" : "FLAGCX Dev", dev,
               commDev->base.ibDevN, ibucDev->portNum, meta.qpInfo[q].qpn,
               devInfo->mtu, devInfo->lid, devInfo->fifoRkey,
               commDev->fifoMr->lkey);
      }
    } else { // RoCE
      for (int q = 0; q < comm->base.nqps; q++) {
        // Print just the QPs for this dev
        if (comm->base.qps[q].devIndex == i)
          INFO(FLAGCX_NET,
               "NET/IBUC: %s %d IbucDev %d Port %d qpn %d mtu %d "
               "query_ece={supported=%d, vendor_id=0x%x, options=0x%x, "
               "comp_mask=0x%x} GID %ld (%lX/%lX) fifoRkey=0x%x fifoLkey=0x%x",
               comm->base.ndevs > 2 ? "FLAGCX MergedDev" : "FLAGCX Dev", dev,
               commDev->base.ibDevN, ibucDev->portNum, meta.qpInfo[q].qpn,
               devInfo->mtu, meta.qpInfo[q].ece_supported,
               meta.qpInfo[q].ece.vendor_id, meta.qpInfo[q].ece.options,
               meta.qpInfo[q].ece.comp_mask,
               (int64_t)commDev->base.gidInfo.localGidIndex, devInfo->spn,
               devInfo->iid, devInfo->fifoRkey, commDev->fifoMr->lkey);
      }
    }
  }
  meta.fifoAddr = (uint64_t)comm->fifo;
  strncpy(meta.devName, mergedDev->devName, MAX_MERGED_DEV_NAME);

  stage->state = flagcxIbCommStateSend;
  stage->offset = 0;
  FLAGCXCHECK(flagcxIbucMalloc((void **)&stage->buffer, sizeof(meta)));

  memcpy(stage->buffer, &meta, sizeof(meta));

ibuc_send:
  FLAGCXCHECK(flagcxSocketProgress(FLAGCX_SOCKET_SEND, &comm->base.sock,
                                   stage->buffer, sizeof(meta),
                                   &stage->offset));
  if (stage->offset != sizeof(meta))
    return flagcxSuccess;

  stage->state = flagcxIbCommStateConnecting;
  stage->offset = 0;
  // Clear the staging buffer for re-use
  memset(stage->buffer, 0, sizeof(meta));

ibuc_connect:
  struct flagcxIbConnectionMetadata remMeta;
  FLAGCXCHECK(
      flagcxSocketProgress(FLAGCX_SOCKET_RECV, &comm->base.sock, stage->buffer,
                           sizeof(flagcxIbConnectionMetadata), &stage->offset));
  if (stage->offset != sizeof(remMeta))
    return flagcxSuccess;

  memcpy(&remMeta, stage->buffer, sizeof(flagcxIbConnectionMetadata));

  comm->base.nRemDevs = remMeta.ndevs;
  if (comm->base.nRemDevs != comm->base.ndevs) {
    mergedDev = flagcxIbMergedDevs + dev;
    WARN(
        "NET/IBUC : Local mergedDev=%s has a different number of devices=%d as "
        "remoteDev=%s nRemDevs=%d",
        mergedDev->devName, comm->base.ndevs, remMeta.devName,
        comm->base.nRemDevs);
  }

  int link_layer;
  link_layer = remMeta.devs[0].link_layer;
  for (int i = 1; i < remMeta.ndevs; i++) {
    if (remMeta.devs[i].link_layer != link_layer) {
      WARN("NET/IBUC : Can't merge net devices with different link_layer. i=%d "
           "remMeta.ndevs=%d link_layer=%d rem_link_layer=%d",
           i, remMeta.ndevs, link_layer, remMeta.devs[i].link_layer);
      return flagcxInternalError;
    }
  }

  // Copy remDevInfo for things like remGidInfo, remFifoAddr, etc.
  for (int i = 0; i < remMeta.ndevs; i++) {
    comm->base.remDevs[i] = remMeta.devs[i];
    comm->base.remDevs[i].remoteGid.global.interface_id =
        comm->base.remDevs[i].iid;
    comm->base.remDevs[i].remoteGid.global.subnet_prefix =
        comm->base.remDevs[i].spn;

    // Retain remote sizes fifo info and prepare RDMA ops
    comm->remSizesFifo.rkeys[i] = remMeta.devs[i].fifoRkey;
    comm->remSizesFifo.addr = remMeta.fifoAddr;
  }

  for (int i = 0; i < comm->base.ndevs; i++) {
    FLAGCXCHECK(
        flagcxWrapIbvRegMr(comm->remSizesFifo.mrs + i, comm->devs[i].base.pd,
                           &comm->remSizesFifo.elems,
                           sizeof(int) * MAX_REQUESTS * FLAGCX_NET_IB_MAX_RECVS,
                           IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE |
                               IBV_ACCESS_REMOTE_READ));
  }
  comm->base.nRemDevs = remMeta.ndevs;

  for (int q = 0; q < comm->base.nqps; q++) {
    struct flagcxIbQpInfo *remQpInfo = remMeta.qpInfo + q;
    struct flagcxIbDevInfo *remDevInfo = remMeta.devs + remQpInfo->devIndex;

    // Assign per-QP remDev
    comm->base.qps[q].remDevIdx = remQpInfo->devIndex;
    int devIndex = comm->base.qps[q].devIndex;
    flagcxIbSendCommDev *commDev = comm->devs + devIndex;
    uint8_t gidIndex = commDev->base.gidInfo.localGidIndex;

    struct ibv_qp *qp = comm->base.qps[q].qp;
    if (remQpInfo->ece_supported && remQpInfo->ece_supported)
      FLAGCXCHECK(
          flagcxWrapIbvSetEce(qp, &remQpInfo->ece, &remQpInfo->ece_supported));

    FLAGCXCHECK(flagcxIbucRtrQp(qp, gidIndex, remQpInfo->qpn, remDevInfo));
    FLAGCXCHECK(flagcxIbucRtsQp(qp));
  }

  if (link_layer == IBV_LINK_LAYER_ETHERNET) { // RoCE
    for (int q = 0; q < comm->base.nqps; q++) {
      struct flagcxIbQp *qp = comm->base.qps + q;
      int ibDevN = comm->devs[qp->devIndex].base.ibDevN;
      struct flagcxIbDev *ibucDev = flagcxIbDevs + ibDevN;
      INFO(FLAGCX_NET,
           "NET/IBUC: IbucDev %d Port %d qpn %d set_ece={supported=%d, "
           "vendor_id=0x%x, options=0x%x, comp_mask=0x%x}",
           ibDevN, ibucDev->portNum, remMeta.qpInfo[q].qpn,
           remMeta.qpInfo[q].ece_supported, remMeta.qpInfo[q].ece.vendor_id,
           remMeta.qpInfo[q].ece.options, remMeta.qpInfo[q].ece.comp_mask);
    }
  }

  FLAGCXCHECK(flagcxIbRetransInit(&comm->retrans));
  comm->last_timeout_check_us = 0;

  if (comm->retrans.enabled && !remMeta.retransEnabled) {
    INFO(FLAGCX_NET,
         "Receiver disabled retransmission, sender will also disable it");
    comm->retrans.enabled = 0;
  }

  if (comm->retrans.enabled) {
    INFO(FLAGCX_INIT | FLAGCX_NET,
         "NET/IBUC Sender: Retransmission ENABLED (RTO=%uus, MaxRetry=%d, "
         "AckInterval=%d)",
         comm->retrans.minRtoUs, comm->retrans.maxRetry,
         comm->retrans.ackInterval);
  } else {
    INFO(FLAGCX_INIT | FLAGCX_NET, "NET/IBUC Sender: Retransmission DISABLED");
  }

  if (comm->retrans.enabled && remMeta.retransEnabled) {
    bool all_ah_success = true;

    for (int i = 0; i < comm->base.ndevs; i++) {
      flagcxIbSendCommDev *commDev = &comm->devs[i];
      flagcxIbDev *ibucDev = flagcxIbDevs + commDev->base.ibDevN;

      TRACE(FLAGCX_NET,
            "Send: Setting up control QP conn for dev %d: remote_qpn=%u, "
            "remote_lid=%u, remote_gid=%lx:%lx, link_layer=%d",
            i, remMeta.ctrlQpn[i], remMeta.ctrlLid[i],
            (unsigned long)remMeta.ctrlGid[i].global.subnet_prefix,
            (unsigned long)remMeta.ctrlGid[i].global.interface_id,
            ibucDev->portAttr.link_layer);

      flagcxResult_t ah_result = flagcxIbSetupCtrlQpConnection(
          ibucDev->context, commDev->base.pd, &commDev->ctrlQp,
          remMeta.ctrlQpn[i], &remMeta.ctrlGid[i], remMeta.ctrlLid[i],
          ibucDev->portNum, ibucDev->portAttr.link_layer,
          commDev->base.gidInfo.localGidIndex);

      if (ah_result != flagcxSuccess || !commDev->ctrlQp.ah) {
        all_ah_success = false;
        break;
      }

      size_t buf_entry_size = sizeof(struct flagcxIbAckMsg) + FLAGCX_IB_ACK_BUF_PADDING;
      for (int r = 0; r < 32; r++) {
        struct ibv_sge sge;
        sge.addr = (uint64_t)((char *)commDev->ackBuffer + r * buf_entry_size);
        sge.length = buf_entry_size;
        sge.lkey = commDev->ackMr->lkey;

        struct ibv_recv_wr recv_wr;
        memset(&recv_wr, 0, sizeof(recv_wr));
        recv_wr.wr_id = r;
        recv_wr.next = NULL;
        recv_wr.sg_list = &sge;
        recv_wr.num_sge = 1;

        struct ibv_recv_wr *bad_wr;
        FLAGCXCHECK(
            flagcxWrapIbvPostRecv(commDev->ctrlQp.qp, &recv_wr, &bad_wr));
      }

      TRACE(FLAGCX_NET,
            "Control QP ready for dev %d: local_qpn=%u, remote_qpn=%u, posted "
            "32 recv WRs",
            i, commDev->ctrlQp.qp->qp_num, remMeta.ctrlQpn[i]);
    }

    if (!all_ah_success) {
      comm->retrans.enabled = 0;

      for (int i = 0; i < comm->base.ndevs; i++) {
        if (comm->devs[i].ackMr)
          flagcxWrapIbvDeregMr(comm->devs[i].ackMr);
        if (comm->devs[i].ackBuffer)
          free(comm->devs[i].ackBuffer);
        flagcxIbDestroyCtrlQp(&comm->devs[i].ctrlQp);
      }
    }

    if (all_ah_success && comm->retrans.enabled) {
      FLAGCXCHECK(flagcxWrapIbvRegMr(
          &comm->retrans_hdr_mr, comm->devs[0].base.pd, comm->retrans_hdr_pool,
          sizeof(comm->retrans_hdr_pool), IBV_ACCESS_LOCAL_WRITE));

      TRACE(FLAGCX_NET,
            "Sender: Initialized SEND retransmission (header pool MR created)");
    }
  }

  comm->outstanding_sends = 0;
  comm->outstanding_retrans = 0;
  comm->max_outstanding = flagcxParamIbMaxOutstanding();

  comm->base.ready = 1;
  stage->state = flagcxIbCommStateConnected;
  stage->offset = 0;

ibuc_send_ready:
  FLAGCXCHECK(flagcxSocketProgress(FLAGCX_SOCKET_SEND, &comm->base.sock,
                                   &comm->base.ready, sizeof(int),
                                   &stage->offset));
  if (stage->offset != sizeof(int))
    return flagcxSuccess;

  free(stage->buffer);
  stage->state = flagcxIbCommStateStart;
  *sendComm = comm;
  return flagcxSuccess;
}

FLAGCX_PARAM(IbucGdrFlushDisable, "GDR_FLUSH_DISABLE", 0);

flagcxResult_t flagcxIbucAccept(void *listenComm, void **recvComm) {
  struct flagcxIbListenComm *lComm = (struct flagcxIbListenComm *)listenComm;
  struct flagcxIbCommStage *stage = &lComm->stage;
  struct flagcxIbRecvComm *rComm = (struct flagcxIbRecvComm *)stage->comm;
  int ready;
  *recvComm = NULL;

  // Pre-declare ALL variables before any goto to avoid crossing initialization
  struct flagcxIbMergedDev *mergedDev;
  struct flagcxIbDev *ibucDev;
  int ibDevN;
  struct flagcxIbRecvCommDev *rCommDev;
  struct flagcxIbDevInfo *remDevInfo;
  struct flagcxIbQp *qp;
  struct ibv_srq *srq = NULL;
  struct flagcxIbConnectionMetadata remMeta;
  struct flagcxIbConnectionMetadata meta;

  if (stage->state == flagcxIbCommStateAccept)
    goto ib_accept_check;
  if (stage->state == flagcxIbCommStateRecv)
    goto ib_recv;
  if (stage->state == flagcxIbCommStateSend)
    goto ibuc_send;
  if (stage->state == flagcxIbCommStatePendingReady)
    goto ib_recv_ready;
  if (stage->state != flagcxIbCommStateStart) {
    WARN("Listencomm in unknown state %d", stage->state);
    return flagcxInternalError;
  }

  FLAGCXCHECK(
      flagcxIbucMalloc((void **)&rComm, sizeof(struct flagcxIbRecvComm)));
  stage->comm = rComm;
  stage->state = flagcxIbCommStateAccept;
  FLAGCXCHECK(flagcxSocketInit(&rComm->base.sock));
  FLAGCXCHECK(flagcxSocketAccept(&rComm->base.sock, &lComm->sock));

ib_accept_check:
  FLAGCXCHECK(flagcxSocketReady(&rComm->base.sock, &ready));
  if (!ready)
    return flagcxSuccess;

  // remMeta already declared at function start
  stage->state = flagcxIbCommStateRecv;
  stage->offset = 0;
  FLAGCXCHECK(flagcxIbucMalloc((void **)&stage->buffer, sizeof(remMeta)));

ib_recv:
  FLAGCXCHECK(flagcxSocketProgress(FLAGCX_SOCKET_RECV, &rComm->base.sock,
                                   stage->buffer, sizeof(remMeta),
                                   &stage->offset));
  if (stage->offset != sizeof(remMeta))
    return flagcxSuccess;

  /* copy back the received info */
  memcpy(&remMeta, stage->buffer, sizeof(struct flagcxIbConnectionMetadata));

  mergedDev = flagcxIbMergedDevs + lComm->dev;
  rComm->base.ndevs = mergedDev->ndevs;
  rComm->base.nqps = flagcxParamIbQpsPerConn() *
                     rComm->base.ndevs; // We must have at least 1 qp per-device
  rComm->base.isSend = false;

  rComm->base.nRemDevs = remMeta.ndevs;
  if (rComm->base.nRemDevs != rComm->base.ndevs) {
    WARN(
        "NET/IBUC : Local mergedDev %s has a different number of devices=%d as "
        "remote %s %d",
        mergedDev->devName, rComm->base.ndevs, remMeta.devName,
        rComm->base.nRemDevs);
  }

  for (int i = 0; i < rComm->base.ndevs; i++) {
    rCommDev = rComm->devs + i;
    ibDevN = mergedDev->devs[i];
    FLAGCXCHECK(flagcxIbucInitCommDevBase(ibDevN, &rCommDev->base));
    ibucDev = flagcxIbDevs + ibDevN;
    FLAGCXCHECK(flagcxIbGetGidIndex(ibucDev->context, ibucDev->portNum,
                                    ibucDev->portAttr.gid_tbl_len,
                                    &rCommDev->base.gidInfo.localGidIndex));
    FLAGCXCHECK(flagcxWrapIbvQueryGid(ibucDev->context, ibucDev->portNum,
                                      rCommDev->base.gidInfo.localGidIndex,
                                      &rCommDev->base.gidInfo.localGid));
  }

  // Copy remDevInfo for things like remGidInfo, remFifoAddr, etc.
  for (int i = 0; i < remMeta.ndevs; i++) {
    rComm->base.remDevs[i] = remMeta.devs[i];
    rComm->base.remDevs[i].remoteGid.global.interface_id =
        rComm->base.remDevs[i].iid;
    rComm->base.remDevs[i].remoteGid.global.subnet_prefix =
        rComm->base.remDevs[i].spn;
  }

  // Create SRQ if retransmission is enabled
  // Similar to UCCL: all data QPs will use SRQ for receiving both normal and
  // retrans data
  srq = NULL;
  if (remMeta.retransEnabled) {
    ibDevN = mergedDev->devs[0];
    ibucDev = flagcxIbDevs + ibDevN;

    flagcxResult_t srq_result = flagcxIbCreateSrq(
        ibucDev->context, rComm->devs[0].base.pd, &rComm->srqMgr);
    if (srq_result == flagcxSuccess) {
      srq = (struct ibv_srq *)rComm->srqMgr.srq;
      TRACE(FLAGCX_NET, "Receiver: Created SRQ for retransmission: srq=%p",
            srq);
    } else {
      INFO(FLAGCX_NET,
           "Receiver: Failed to create SRQ (result=%d), disabling "
           "retransmission",
           srq_result);
      remMeta.retransEnabled = 0;
      meta.retransEnabled = 0;
      rComm->retrans.enabled = 0;
      srq = NULL;
    }
  }

  // Stripe QP creation across merged devs
  // Make sure to get correct remote peer dev and QP info
  int remDevIdx;
  int devIndex;
  devIndex = 0;
  for (int q = 0; q < rComm->base.nqps; q++) {
    remDevIdx = remMeta.qpInfo[q].devIndex;
    remDevInfo = remMeta.devs + remDevIdx;
    qp = rComm->base.qps + q;
    rCommDev = rComm->devs + devIndex;
    qp->remDevIdx = remDevIdx;

    // Local ibDevN
    ibDevN = rComm->devs[devIndex].base.ibDevN;
    ibucDev = flagcxIbDevs + ibDevN;

    FLAGCXCHECK(flagcxIbucCreateQpWithTypeSrq(ibucDev->portNum, &rCommDev->base,
                                              IBV_ACCESS_REMOTE_WRITE,
                                              IBV_QPT_UC, qp, srq));
    qp->devIndex = devIndex;
    devIndex = (devIndex + 1) % rComm->base.ndevs;

    // Set the ece (enhanced connection establishment) on this QP before RTR
    if (remMeta.qpInfo[q].ece_supported) {
      FLAGCXCHECK(flagcxWrapIbvSetEce(qp->qp, &remMeta.qpInfo[q].ece,
                                      &meta.qpInfo[q].ece_supported));

      // Query the reduced ece for this QP (matching enhancements between the
      // requestor and the responder) Store this in our own qpInfo for returning
      // to the requestor
      if (meta.qpInfo[q].ece_supported)
        FLAGCXCHECK(flagcxWrapIbvQueryEce(qp->qp, &meta.qpInfo[q].ece,
                                          &meta.qpInfo[q].ece_supported));
    }

    FLAGCXCHECK(flagcxIbucRtrQp(qp->qp, rCommDev->base.gidInfo.localGidIndex,
                                remMeta.qpInfo[q].qpn, remDevInfo));
    FLAGCXCHECK(flagcxIbucRtsQp(qp->qp));
  }

  rComm->flushEnabled = 1;

  for (int i = 0; i < mergedDev->ndevs; i++) {
    rCommDev = rComm->devs + i;
    ibDevN = rCommDev->base.ibDevN;
    ibucDev = flagcxIbDevs + ibDevN;

    // Retain remote fifo info and prepare my RDMA ops
    rCommDev->fifoRkey = remMeta.devs[i].fifoRkey;
    rComm->remFifo.addr = remMeta.fifoAddr;
    FLAGCXCHECK(flagcxWrapIbvRegMr(
        &rCommDev->fifoMr, rCommDev->base.pd, &rComm->remFifo.elems,
        sizeof(struct flagcxIbSendFifo) * MAX_REQUESTS *
            FLAGCX_NET_IB_MAX_RECVS,
        IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE |
            IBV_ACCESS_REMOTE_READ));
    rCommDev->fifoSge.lkey = rCommDev->fifoMr->lkey;
    if (flagcxParamIbUseInline())
      rComm->remFifo.flags = IBV_SEND_INLINE;

    // Allocate Flush dummy buffer for GPU Direct RDMA
    if (rComm->flushEnabled) {
      FLAGCXCHECK(flagcxWrapIbvRegMr(&rCommDev->gpuFlush.hostMr,
                                     rCommDev->base.pd, &rComm->gpuFlushHostMem,
                                     sizeof(int), IBV_ACCESS_LOCAL_WRITE));
      rCommDev->gpuFlush.sge.addr = (uint64_t)&rComm->gpuFlushHostMem;
      rCommDev->gpuFlush.sge.length = 1;
      rCommDev->gpuFlush.sge.lkey = rCommDev->gpuFlush.hostMr->lkey;
      FLAGCXCHECK(flagcxIbucCreateQpWithType(
          ibucDev->portNum, &rCommDev->base,
          IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ, IBV_QPT_RC,
          &rCommDev->gpuFlush.qp));
      struct flagcxIbDevInfo devInfo;
      devInfo.lid = ibucDev->portAttr.lid;
      devInfo.link_layer = ibucDev->portAttr.link_layer;
      devInfo.ib_port = ibucDev->portNum;
      devInfo.spn = rCommDev->base.gidInfo.localGid.global.subnet_prefix;
      devInfo.iid = rCommDev->base.gidInfo.localGid.global.interface_id;
      devInfo.mtu = ibucDev->portAttr.active_mtu;
      FLAGCXCHECK(flagcxIbucRtrQpWithType(
          rCommDev->gpuFlush.qp.qp, rCommDev->base.gidInfo.localGidIndex,
          rCommDev->gpuFlush.qp.qp->qp_num, &devInfo, IBV_QPT_RC));
      FLAGCXCHECK(
          flagcxIbucRtsQpWithType(rCommDev->gpuFlush.qp.qp, IBV_QPT_RC));
    }

    if (remMeta.retransEnabled && meta.retransEnabled) {
      FLAGCXCHECK(flagcxIbCreateCtrlQp(ibucDev->context, rCommDev->base.pd,
                                       ibucDev->portNum, &rCommDev->ctrlQp));
      meta.ctrlQpn[i] = rCommDev->ctrlQp.qp->qp_num;
      meta.ctrlLid[i] = ibucDev->portAttr.lid;
      meta.ctrlGid[i] = rCommDev->base.gidInfo.localGid;

      TRACE(FLAGCX_NET,
            "Receiver: Control QP created for dev %d, qpn=%u, lid=%u", i,
            meta.ctrlQpn[i], meta.ctrlLid[i]);

      size_t ack_buf_size = (sizeof(struct flagcxIbAckMsg) + FLAGCX_IB_ACK_BUF_PADDING) * FLAGCX_IB_ACK_BUF_COUNT;
      rCommDev->ackBuffer = malloc(ack_buf_size);
      FLAGCXCHECK(flagcxWrapIbvRegMr(&rCommDev->ackMr, rCommDev->base.pd,
                                     rCommDev->ackBuffer, ack_buf_size,
                                     IBV_ACCESS_LOCAL_WRITE));

      TRACE(FLAGCX_NET,
            "Recv: Setting up control QP conn for dev %d: remote_qpn=%u, "
            "remote_lid=%u, remote_gid=%lx:%lx, link_layer=%d",
            i, remMeta.ctrlQpn[i], remMeta.ctrlLid[i],
            (unsigned long)remMeta.ctrlGid[i].global.subnet_prefix,
            (unsigned long)remMeta.ctrlGid[i].global.interface_id,
            ibucDev->portAttr.link_layer);

      flagcxResult_t ah_result = flagcxIbSetupCtrlQpConnection(
          ibucDev->context, rCommDev->base.pd, &rCommDev->ctrlQp,
          remMeta.ctrlQpn[i], &remMeta.ctrlGid[i], remMeta.ctrlLid[i],
          ibucDev->portNum, ibucDev->portAttr.link_layer,
          rCommDev->base.gidInfo.localGidIndex);

      if (ah_result != flagcxSuccess || !rCommDev->ctrlQp.ah) {
        INFO(FLAGCX_NET,
             "Receiver Control QP setup failed for dev %d, disabling "
             "retransmission",
             i);
        rComm->retrans.enabled = 0;
        meta.retransEnabled = 0;

        if (rCommDev->ackMr)
          flagcxWrapIbvDeregMr(rCommDev->ackMr);
        if (rCommDev->ackBuffer)
          free(rCommDev->ackBuffer);
        flagcxIbDestroyCtrlQp(&rCommDev->ctrlQp);
      } else {
        TRACE(FLAGCX_NET,
              "Receiver Control QP successfully initialized for dev %d (ah=%p)",
              i, rCommDev->ctrlQp.ah);
      }
    }

    // Fill Handle
    meta.devs[i].lid = ibucDev->portAttr.lid;
    meta.devs[i].link_layer = rCommDev->base.gidInfo.link_layer =
        ibucDev->portAttr.link_layer;
    meta.devs[i].ib_port = ibucDev->portNum;
    meta.devs[i].spn = rCommDev->base.gidInfo.localGid.global.subnet_prefix;
    meta.devs[i].iid = rCommDev->base.gidInfo.localGid.global.interface_id;

    // Adjust the MTU
    remMeta.devs[i].mtu = (enum ibv_mtu)std::min(remMeta.devs[i].mtu,
                                                 ibucDev->portAttr.active_mtu);
    meta.devs[i].mtu = remMeta.devs[i].mtu;

    // Prepare sizes fifo
    FLAGCXCHECK(flagcxWrapIbvRegMr(
        &rComm->devs[i].sizesFifoMr, rComm->devs[i].base.pd, rComm->sizesFifo,
        sizeof(int) * MAX_REQUESTS * FLAGCX_NET_IB_MAX_RECVS,
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
            IBV_ACCESS_REMOTE_READ));
    meta.devs[i].fifoRkey = rComm->devs[i].sizesFifoMr->rkey;
  }
  meta.fifoAddr = (uint64_t)rComm->sizesFifo;

  for (int q = 0; q < rComm->base.nqps; q++) {
    meta.qpInfo[q].qpn = rComm->base.qps[q].qp->qp_num;
    meta.qpInfo[q].devIndex = rComm->base.qps[q].devIndex;
  }

  meta.ndevs = rComm->base.ndevs;
  meta.retransEnabled = remMeta.retransEnabled;
  strncpy(meta.devName, mergedDev->devName, MAX_MERGED_DEV_NAME);

  stage->state = flagcxIbCommStateSend;
  stage->offset = 0;
  if (stage->buffer)
    free(stage->buffer);
  FLAGCXCHECK(flagcxIbucMalloc((void **)&stage->buffer,
                               sizeof(struct flagcxIbConnectionMetadata)));
  memcpy(stage->buffer, &meta, sizeof(struct flagcxIbConnectionMetadata));

ibuc_send:
  FLAGCXCHECK(flagcxSocketProgress(
      FLAGCX_SOCKET_SEND, &rComm->base.sock, stage->buffer,
      sizeof(struct flagcxIbConnectionMetadata), &stage->offset));
  if (stage->offset < sizeof(struct flagcxIbConnectionMetadata))
    return flagcxSuccess;

  stage->offset = 0;
  stage->state = flagcxIbCommStatePendingReady;

ib_recv_ready:
  FLAGCXCHECK(flagcxSocketProgress(FLAGCX_SOCKET_RECV, &rComm->base.sock,
                                   &rComm->base.ready, sizeof(int),
                                   &stage->offset));
  if (stage->offset != sizeof(int))
    return flagcxSuccess;

  FLAGCXCHECK(flagcxIbRetransInit(&rComm->retrans));

  if (meta.retransEnabled == 0 && rComm->retrans.enabled) {
    INFO(FLAGCX_NET, "Receiver: Remote disabled retransmission, disabling "
                     "local retransmission");
    rComm->retrans.enabled = 0;
  }

  // Initialize SRQ with recv buffers (similar to UCCL)
  if (rComm->retrans.enabled && rComm->srqMgr.srq != NULL) {
    rComm->srqMgr.postSrqCount = FLAGCX_IB_SRQ_SIZE;

    // Post in batches until all are posted
    while (rComm->srqMgr.postSrqCount > 0) {
      FLAGCXCHECK(flagcxIbSrqPostRecv(&rComm->srqMgr, FLAGCX_IB_ACK_BUF_COUNT));
    }

    INFO(FLAGCX_INIT | FLAGCX_NET,
         "NET/IBUC Receiver: Retransmission ENABLED (Posted %d recv WRs to SRQ "
         "for retransmission)",
         FLAGCX_IB_SRQ_SIZE);
  } else {
    INFO(FLAGCX_INIT | FLAGCX_NET,
         "NET/IBUC Receiver: Retransmission DISABLED");
  }

  free(stage->buffer);
  *recvComm = rComm;

  /* reset lComm stage */
  stage->state = flagcxIbCommStateStart;
  stage->offset = 0;
  stage->comm = NULL;
  stage->buffer = NULL;
  return flagcxSuccess;
}

flagcxResult_t flagcxIbucGetRequest(struct flagcxIbNetCommBase *base,
                                    struct flagcxIbRequest **req) {
  for (int i = 0; i < MAX_REQUESTS; i++) {
    struct flagcxIbRequest *r = base->reqs + i;
    if (r->type == FLAGCX_NET_IB_REQ_UNUSED) {
      r->base = base;
      r->sock = NULL;
      r->devBases[0] = NULL;
      r->devBases[1] = NULL;
      r->events[0] = r->events[1] = 0;
      *req = r;
      return flagcxSuccess;
    }
  }
  WARN("NET/IBUC : unable to allocate requests");
  *req = NULL;
  return flagcxInternalError;
}

flagcxResult_t flagcxIbucRegMrDmaBufInternal(flagcxIbNetCommDevBase *base,
                                             void *data, size_t size, int type,
                                             uint64_t offset, int fd,
                                             ibv_mr **mhandle) {
  static __thread uintptr_t pageSize = 0;
  if (pageSize == 0)
    pageSize = sysconf(_SC_PAGESIZE);
  struct flagcxIbMrCache *cache = &flagcxIbDevs[base->ibDevN].mrCache;
  uintptr_t addr = (uintptr_t)data & -pageSize;
  size_t pages = ((uintptr_t)data + size - addr + pageSize - 1) / pageSize;
  flagcxResult_t res;
  pthread_mutex_lock(&flagcxIbDevs[base->ibDevN].lock);
  for (int slot = 0; /*true*/; slot++) {
    if (slot == cache->population ||
        addr < cache->slots[slot].addr) {         // didn't find in cache
      if (cache->population == cache->capacity) { // must grow cache
        cache->capacity = cache->capacity < 32 ? 32 : 2 * cache->capacity;
        FLAGCXCHECKGOTO(
            flagcxRealloc(&cache->slots, cache->population, cache->capacity),
            res, returning);
      }
      // Deregister / register
      struct ibv_mr *mr;
      unsigned int flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE |
                           IBV_ACCESS_REMOTE_READ;
      if (flagcxIbRelaxedOrderingEnabled)
        flags |= IBV_ACCESS_RELAXED_ORDERING;
      if (fd != -1) {
        /* DMA-BUF support */
        FLAGCXCHECKGOTO(flagcxWrapIbvRegDmabufMr(&mr, base->pd, offset,
                                                 pages * pageSize, addr, fd,
                                                 flags),
                        res, returning);
      } else {
        void *cpuptr = NULL;
        if (deviceAdaptor->gdrPtrMmap && deviceAdaptor->gdrPtrMunmap) {
          deviceAdaptor->gdrPtrMmap(&cpuptr, (void *)addr, pages * pageSize);
        }
        if (flagcxIbRelaxedOrderingEnabled) {
          // Use IBVERBS_1.8 API - needed for IBV_ACCESS_RELAXED_ORDERING
          // support
          FLAGCXCHECKGOTO(
              flagcxWrapIbvRegMrIova2(&mr, base->pd,
                                      cpuptr == NULL ? (void *)addr : cpuptr,
                                      pages * pageSize, addr, flags),
              res, returning);
        } else {
          FLAGCXCHECKGOTO(
              flagcxWrapIbvRegMr(&mr, base->pd,
                                 cpuptr == NULL ? (void *)addr : cpuptr,
                                 pages * pageSize, flags),
              res, returning);
        }
        if (deviceAdaptor->gdrPtrMmap && deviceAdaptor->gdrPtrMunmap) {
          deviceAdaptor->gdrPtrMunmap(cpuptr, pages * pageSize);
        }
      }
      TRACE(FLAGCX_INIT | FLAGCX_NET,
            "regAddr=0x%lx size=%lld rkey=0x%x lkey=0x%x fd=%d",
            (unsigned long)addr, (long long)pages * pageSize, mr->rkey,
            mr->lkey, fd);
      if (slot != cache->population)
        memmove(cache->slots + slot + 1, cache->slots + slot,
                (cache->population - slot) * sizeof(struct flagcxIbMr));
      cache->slots[slot].addr = addr;
      cache->slots[slot].pages = pages;
      cache->slots[slot].refs = 1;
      cache->slots[slot].mr = mr;
      cache->population += 1;
      *mhandle = mr;
      res = flagcxSuccess;
      goto returning;
    } else if ((addr >= cache->slots[slot].addr) &&
               ((addr - cache->slots[slot].addr) / pageSize + pages) <=
                   cache->slots[slot].pages) {
      cache->slots[slot].refs += 1;
      *mhandle = cache->slots[slot].mr;
      res = flagcxSuccess;
      goto returning;
    }
  }
returning:
  pthread_mutex_unlock(&flagcxIbDevs[base->ibDevN].lock);
  return res;
}

struct flagcxIbNetCommDevBase *
flagcxIbucGetNetCommDevBase(flagcxIbNetCommBase *base, int devIndex) {
  if (base->isSend) {
    struct flagcxIbSendComm *sComm = (struct flagcxIbSendComm *)base;
    return &sComm->devs[devIndex].base;
  } else {
    struct flagcxIbRecvComm *rComm = (struct flagcxIbRecvComm *)base;
    return &rComm->devs[devIndex].base;
  }
}

/* DMA-BUF support */
flagcxResult_t flagcxIbucRegMrDmaBuf(void *comm, void *data, size_t size,
                                     int type, uint64_t offset, int fd,
                                     void **mhandle) {
  assert(size > 0);
  struct flagcxIbNetCommBase *base = (struct flagcxIbNetCommBase *)comm;
  struct flagcxIbMrHandle *mhandleWrapper =
      (struct flagcxIbMrHandle *)malloc(sizeof(struct flagcxIbMrHandle));
  for (int i = 0; i < base->ndevs; i++) {
    struct flagcxIbNetCommDevBase *devComm =
        flagcxIbucGetNetCommDevBase(base, i);
    FLAGCXCHECK(flagcxIbucRegMrDmaBufInternal(devComm, data, size, type, offset,
                                              fd, mhandleWrapper->mrs + i));
  }
  *mhandle = (void *)mhandleWrapper;
  return flagcxSuccess;
}

flagcxResult_t flagcxIbucRegMr(void *comm, void *data, size_t size, int type,
                               void **mhandle) {
  return flagcxIbucRegMrDmaBuf(comm, data, size, type, 0ULL, -1, mhandle);
}

flagcxResult_t flagcxIbucDeregMrInternal(flagcxIbNetCommDevBase *base,
                                         ibv_mr *mhandle) {
  struct flagcxIbMrCache *cache = &flagcxIbDevs[base->ibDevN].mrCache;
  flagcxResult_t res;
  pthread_mutex_lock(&flagcxIbDevs[base->ibDevN].lock);
  for (int i = 0; i < cache->population; i++) {
    if (mhandle == cache->slots[i].mr) {
      if (0 == --cache->slots[i].refs) {
        memmove(&cache->slots[i], &cache->slots[--cache->population],
                sizeof(struct flagcxIbMr));
        if (cache->population == 0) {
          free(cache->slots);
          cache->slots = NULL;
          cache->capacity = 0;
        }
        FLAGCXCHECKGOTO(flagcxWrapIbvDeregMr(mhandle), res, returning);
      }
      res = flagcxSuccess;
      goto returning;
    }
  }
  WARN("NET/IBUC: could not find mr %p inside cache of %d entries", mhandle,
       cache->population);
  res = flagcxInternalError;
returning:
  pthread_mutex_unlock(&flagcxIbDevs[base->ibDevN].lock);
  return res;
}

flagcxResult_t flagcxIbucDeregMr(void *comm, void *mhandle) {
  struct flagcxIbMrHandle *mhandleWrapper = (struct flagcxIbMrHandle *)mhandle;
  struct flagcxIbNetCommBase *base = (struct flagcxIbNetCommBase *)comm;
  for (int i = 0; i < base->ndevs; i++) {
    struct flagcxIbNetCommDevBase *devComm =
        flagcxIbucGetNetCommDevBase(base, i);
    FLAGCXCHECK(flagcxIbucDeregMrInternal(devComm, mhandleWrapper->mrs[i]));
  }
  free(mhandleWrapper);
  return flagcxSuccess;
}

FLAGCX_PARAM(IbucSplitDataOnQps, "IBUC_SPLIT_DATA_ON_QPS", 0);

flagcxResult_t flagcxIbucMultiSend(struct flagcxIbSendComm *comm, int slot) {
  struct flagcxIbRequest **reqs = comm->fifoReqs[slot];
  volatile struct flagcxIbSendFifo *slots = comm->fifo[slot];
  int nreqs = slots[0].nreqs;
  if (nreqs > FLAGCX_NET_IB_MAX_RECVS)
    return flagcxInternalError;

  uint64_t wr_id = 0ULL;
  for (int r = 0; r < nreqs; r++) {
    struct ibv_send_wr *wr = comm->wrs + r;
    memset(wr, 0, sizeof(struct ibv_send_wr));

    struct ibv_sge *sge = comm->sges + r;
    sge->addr = (uintptr_t)reqs[r]->send.data;
    wr->opcode = IBV_WR_RDMA_WRITE;
    wr->send_flags = 0;
    wr->wr.rdma.remote_addr = slots[r].addr;
    wr->next = wr + 1;
    wr_id += (reqs[r] - comm->base.reqs) << (r * 8);
  }

  // Write size as immediate data. In the case of multi-send, only write
  // 0 or 1 as size to indicate whether there was data sent or received.
  uint32_t immData = 0;
  uint32_t seq = 0;

  if (nreqs == 1) {
    if (comm->retrans.enabled) {
      seq = comm->retrans.sendSeq;
      comm->retrans.sendSeq = (comm->retrans.sendSeq + 1) & 0xFFFF;
      immData = flagcxIbEncodeImmData(seq, reqs[0]->send.size);
      TRACE(FLAGCX_NET,
            "Sending packet with SEQ: seq=%u, size=%u, immData=0x%x", seq,
            reqs[0]->send.size, immData);
    } else {
      immData = reqs[0]->send.size;
    }
  } else {
    int *sizes = comm->remSizesFifo.elems[slot];
    for (int r = 0; r < nreqs; r++)
      sizes[r] = reqs[r]->send.size;
    comm->remSizesFifo.sge.addr = (uint64_t)sizes;
    comm->remSizesFifo.sge.length = nreqs * sizeof(int);
  }

  struct ibv_send_wr *lastWr = comm->wrs + nreqs - 1;
  if (nreqs > 1 ||
      (comm->ar && reqs[0]->send.size > flagcxParamIbArThreshold())) {
    // When using ADAPTIVE_ROUTING, send the bulk of the data first as an
    // RDMA_WRITE, then a 0-byte RDMA_WRITE_WITH_IMM to trigger a remote
    // completion.
    lastWr++;
    memset(lastWr, 0, sizeof(struct ibv_send_wr));
    if (nreqs > 1) {
      // Write remote sizes Fifo
      lastWr->wr.rdma.remote_addr =
          comm->remSizesFifo.addr +
          slot * FLAGCX_NET_IB_MAX_RECVS * sizeof(int);
      lastWr->num_sge = 1;
      lastWr->sg_list = &comm->remSizesFifo.sge;
    }
  }
  lastWr->wr_id = wr_id;
  lastWr->opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
  lastWr->imm_data = immData;
  lastWr->next = NULL;
  lastWr->send_flags = IBV_SEND_SIGNALED;

  // Multi-QP: make sure IB writes are multiples of 128B so that LL and LL128
  // protocols still work
  const int align = 128;
  int nqps =
      flagcxParamIbucSplitDataOnQps() ? comm->base.nqps : comm->base.ndevs;
  for (int i = 0; i < nqps; i++) {
    int qpIndex = comm->base.qpIndex;
    flagcxIbQp *qp = comm->base.qps + qpIndex;
    int devIndex = qp->devIndex;
    for (int r = 0; r < nreqs; r++) {
      comm->wrs[r].wr.rdma.rkey = slots[r].rkeys[qp->remDevIdx];

      int chunkSize = DIVUP(DIVUP(reqs[r]->send.size, nqps), align) * align;
      int length =
          std::min(reqs[r]->send.size - reqs[r]->send.offset, chunkSize);
      if (length <= 0) {
        comm->wrs[r].sg_list = NULL;
        comm->wrs[r].num_sge = 0;
      } else {
        // Select proper lkey
        comm->sges[r].lkey = reqs[r]->send.lkeys[devIndex];
        comm->sges[r].length = length;
        comm->wrs[r].sg_list = comm->sges + r;
        comm->wrs[r].num_sge = 1;
      }
    }

    if (nreqs > 1) {
      // Also make sure lastWr writes remote sizes using the right lkey
      comm->remSizesFifo.sge.lkey = comm->remSizesFifo.mrs[devIndex]->lkey;
      lastWr->wr.rdma.rkey = comm->remSizesFifo.rkeys[devIndex];
    }

    struct ibv_send_wr *bad_wr;
    FLAGCXCHECK(flagcxWrapIbvPostSend(qp->qp, comm->wrs, &bad_wr));

    for (int r = 0; r < nreqs; r++) {
      int chunkSize = DIVUP(DIVUP(reqs[r]->send.size, nqps), align) * align;
      reqs[r]->send.offset += chunkSize;
      comm->sges[r].addr += chunkSize;
      comm->wrs[r].wr.rdma.remote_addr += chunkSize;
    }

    // Select the next qpIndex
    comm->base.qpIndex = (comm->base.qpIndex + 1) % comm->base.nqps;
  }

  if (comm->retrans.enabled && nreqs == 1) {
    FLAGCXCHECK(flagcxIbRetransAddPacket(
        &comm->retrans, seq, reqs[0]->send.size, reqs[0]->send.data,
        slots[0].addr, // remote_addr
        reqs[0]->send.lkeys, (uint32_t *)slots[0].rkeys));
  }

  comm->outstanding_sends++;

  return flagcxSuccess;
}

flagcxResult_t flagcxIbucIsend(void *sendComm, void *data, size_t size, int tag,
                               void *mhandle, void *phandle, void **request) {
  struct flagcxIbSendComm *comm = (struct flagcxIbSendComm *)sendComm;
  if (comm->base.ready == 0) {
    WARN("NET/IBUC: flagcxIbucIsend() called when comm->base.ready == 0");
    return flagcxInternalError;
  }

  // Flow control: Check if we have too many outstanding sends before matching
  // request This prevents retransmission buffer overflow when ACKs are delayed
  int total_outstanding = comm->outstanding_sends + comm->outstanding_retrans;
  if (total_outstanding >= comm->max_outstanding) {
    TRACE(FLAGCX_NET,
          "Flow control: outstanding=%d (sends=%d + retrans=%d) >= max=%d, "
          "deferring send",
          total_outstanding, comm->outstanding_sends, comm->outstanding_retrans,
          comm->max_outstanding);
    *request = NULL;
    return flagcxSuccess;
  }

  struct flagcxIbMrHandle *mhandleWrapper = (struct flagcxIbMrHandle *)mhandle;

  // Wait for the receiver to have posted the corresponding receive
  int nreqs = 0;
  volatile struct flagcxIbSendFifo *slots;

  int slot = (comm->fifoHead) % MAX_REQUESTS;
  struct flagcxIbRequest **reqs = comm->fifoReqs[slot];
  slots = comm->fifo[slot];
  uint64_t idx = comm->fifoHead + 1;
  if (slots[0].idx != idx) {
    *request = NULL;
    return flagcxSuccess;
  }
  nreqs = slots[0].nreqs;
  // Wait until all data has arrived
  for (int r = 1; r < nreqs; r++)
    while (slots[r].idx != idx)
      ;
  __sync_synchronize(); // order the nreqsPtr load against tag/rkey/addr loads
                        // below
  for (int r = 0; r < nreqs; r++) {
    if (reqs[r] != NULL || slots[r].tag != tag)
      continue;

    if (size > slots[r].size)
      size = slots[r].size;
    // Sanity checks
    if (slots[r].size < 0 || slots[r].addr == 0 || slots[r].rkeys[0] == 0) {
      char line[SOCKET_NAME_MAXLEN + 1];
      union flagcxSocketAddress addr;
      flagcxSocketGetAddr(&comm->base.sock, &addr);
      WARN("NET/IBUC : req %d/%d tag %x peer %s posted incorrect receive info: "
           "size %ld addr %lx rkeys[0]=%x",
           r, nreqs, tag, flagcxSocketToString(&addr, line), slots[r].size,
           slots[r].addr, slots[r].rkeys[0]);
      return flagcxInternalError;
    }

    struct flagcxIbRequest *req;
    FLAGCXCHECK(flagcxIbucGetRequest(&comm->base, &req));
    req->type = FLAGCX_NET_IB_REQ_SEND;
    req->sock = &comm->base.sock;
    req->base = &comm->base;
    req->nreqs = nreqs;
    req->send.size = size;
    req->send.data = data;
    req->send.offset = 0;

    // Populate events
    int nEvents =
        flagcxParamIbucSplitDataOnQps() ? comm->base.nqps : comm->base.ndevs;
    int qpIndex = comm->base.qpIndex;
    // Count down
    while (nEvents > 0) {
      flagcxIbQp *qp = comm->base.qps + qpIndex;
      int devIndex = qp->devIndex;
      flagcxIbucAddEvent(req, devIndex, &comm->devs[devIndex].base);
      // Track the valid lkey for this RDMA_Write
      req->send.lkeys[devIndex] = mhandleWrapper->mrs[devIndex]->lkey;
      nEvents--;
      // Don't update comm->base.qpIndex yet, we need to run through this same
      // set of QPs inside flagcxIbucMultiSend()
      qpIndex = (qpIndex + 1) % comm->base.nqps;
    }

    // Store all lkeys
    for (int i = 0; i < comm->base.ndevs; i++) {
      req->send.lkeys[i] = mhandleWrapper->mrs[i]->lkey;
    }

    *request = reqs[r] = req;

    // If this is a multi-recv, send only when all requests have matched.
    for (int r = 0; r < nreqs; r++) {
      if (reqs[r] == NULL)
        return flagcxSuccess;
    }

    TIME_START(0);
    FLAGCXCHECK(flagcxIbucMultiSend(comm, slot));

    // Clear slots[0]->nreqs, as well as other fields to help debugging and
    // sanity checks
    memset((void *)slots, 0, sizeof(struct flagcxIbSendFifo));
    memset(reqs, 0, FLAGCX_NET_IB_MAX_RECVS * sizeof(struct flagcxIbRequest *));
    comm->fifoHead++;
    TIME_STOP(0);
    return flagcxSuccess;
  }

  *request = NULL;
  return flagcxSuccess;
}

flagcxResult_t flagcxIbucPostFifo(struct flagcxIbRecvComm *comm, int n,
                                  void **data, size_t *sizes, int *tags,
                                  void **mhandles,
                                  struct flagcxIbRequest *req) {
  struct ibv_send_wr wr;
  memset(&wr, 0, sizeof(wr));

  int slot = comm->remFifo.fifoTail % MAX_REQUESTS;
  req->recv.sizes = comm->sizesFifo[slot];
  for (int i = 0; i < n; i++)
    req->recv.sizes[i] = 0;
  struct flagcxIbSendFifo *localElem = comm->remFifo.elems[slot];

  // Select the next devIndex (local) and QP to use for posting this CTS message
  // Since QPs are initialized by striping across devIndex, we can simply assign
  // this to the same value
  flagcxIbQp *ctsQp = comm->base.qps + comm->base.devIndex;
  comm->base.devIndex = (comm->base.devIndex + 1) % comm->base.ndevs;

  for (int i = 0; i < n; i++) {
    localElem[i].addr = (uint64_t)data[i];
    struct flagcxIbMrHandle *mhandleWrapper =
        (struct flagcxIbMrHandle *)mhandles[i];

    // Send all applicable rkeys
    for (int j = 0; j < comm->base.ndevs; j++)
      localElem[i].rkeys[j] = mhandleWrapper->mrs[j]->rkey;
    localElem[i].nreqs = n;
    localElem[i].size = sizes[i];
    localElem[i].tag = tags[i];
    localElem[i].idx = comm->remFifo.fifoTail + 1;
  }
  wr.wr.rdma.remote_addr =
      comm->remFifo.addr +
      slot * FLAGCX_NET_IB_MAX_RECVS * sizeof(struct flagcxIbSendFifo);

  // Lookup the correct fifoRkey
  wr.wr.rdma.rkey = comm->base.remDevs[ctsQp->remDevIdx].fifoRkey;

  // Set the correct sge properties
  comm->devs[ctsQp->devIndex].fifoSge.addr = (uint64_t)localElem;
  comm->devs[ctsQp->devIndex].fifoSge.length =
      n * sizeof(struct flagcxIbSendFifo);
  wr.sg_list = &comm->devs[ctsQp->devIndex].fifoSge;
  wr.num_sge = 1;

  wr.opcode = IBV_WR_RDMA_WRITE;       // UC mode supports RDMA_WRITE
  wr.send_flags = comm->remFifo.flags; // IBV_SEND_INLINE

  // We need to occasionally post a request with the IBV_SEND_SIGNALED flag,
  // otherwise the send queue will never empty.
  //
  // From https://www.rdmamojo.com/2014/06/30/working-unsignaled-completions/
  // "How to use Unsignaled Completion?" / "Gotchas and Pitfalls"
  // All posted Send Requested, Signaled and Unsignaled, are considered
  // outstanding until a Work Completion that they, or Send Requests that were
  // posted after them, was polled from the Completion Queue associated with the
  // Send Queue. This means if one works with a Queue Pair that was configured
  // to work with Unsignaled Completions, he must make sure that occasionally
  // (before the Send Queue is full with outstanding Send Requests) a Send
  // Request that generate Work Completion will be posted.
  //
  // Not following this rule may lead to a case that the Send Queue is full with
  // Send Requests that won't generate Work Completion:
  //
  //  - The Send Queue is full, so no new Send Requests can be posted to it
  //  - The Send Queue can't be emptied, since no Work Completion can be
  //  generated anymore
  //    (the reason is that no Work Completion, that can generate Work
  //    Completion that polling it will empty the Send Queue, can be posted)
  //  - The status of all posted Send Request is considered unknown
  //
  // slot == devIndex - When writing to fifo slot N, and this QP lives on device
  // index N, it should send signalled. This works out that each fifo posting QP
  // gets drained
  if (slot == ctsQp->devIndex) {
    wr.send_flags |= IBV_SEND_SIGNALED;
    wr.wr_id = req - comm->base.reqs;
    flagcxIbucAddEvent(req, ctsQp->devIndex, &comm->devs[ctsQp->devIndex].base);
  }

  struct ibv_send_wr *bad_wr;
  FLAGCXCHECK(flagcxWrapIbvPostSend(ctsQp->qp, &wr, &bad_wr));
  comm->remFifo.fifoTail++;

  return flagcxSuccess;
}

flagcxResult_t flagcxIbucIrecv(void *recvComm, int n, void **data,
                               size_t *sizes, int *tags, void **mhandles,
                               void **phandles, void **request) {
  struct flagcxIbRecvComm *comm = (struct flagcxIbRecvComm *)recvComm;
  if (comm->base.ready == 0) {
    WARN("NET/IBUC: flagcxIbucIrecv() called when comm->base.ready == 0");
    return flagcxInternalError;
  }
  if (n > FLAGCX_NET_IB_MAX_RECVS)
    return flagcxInternalError;

  struct flagcxIbRequest *req;
  FLAGCXCHECK(flagcxIbucGetRequest(&comm->base, &req));
  req->type = FLAGCX_NET_IB_REQ_RECV;
  req->sock = &comm->base.sock;
  req->nreqs = n;

  for (int i = 0; i < comm->base.ndevs; i++) {
    req->devBases[i] = &comm->devs[i].base;
  }

  struct ibv_recv_wr wr;
  memset(&wr, 0, sizeof(wr));
  wr.wr_id = req - comm->base.reqs;
  wr.sg_list = NULL;
  wr.num_sge = 0;

  TIME_START(1);
  // Select either all QPs, or one qp per-device
  const int nqps =
      flagcxParamIbucSplitDataOnQps() ? comm->base.nqps : comm->base.ndevs;

  // Post recvs - skip if using SRQ (similar to UCCL)
  // When using SRQ, all QPs share SRQ for recv, don't post per-request recv
  if (!comm->retrans.enabled || comm->srqMgr.srq == NULL) {
    // Normal mode: post recv to each QP
    struct ibv_recv_wr *bad_wr;
    for (int i = 0; i < nqps; i++) {
      struct flagcxIbQp *qp = comm->base.qps + comm->base.qpIndex;
      flagcxIbucAddEvent(req, qp->devIndex, &comm->devs[qp->devIndex].base);
      FLAGCXCHECK(flagcxWrapIbvPostRecv(qp->qp, &wr, &bad_wr));
      comm->base.qpIndex = (comm->base.qpIndex + 1) % comm->base.nqps;
    }
  } else {
    // SRQ mode: don't post recv, but still set up events for completion
    for (int i = 0; i < nqps; i++) {
      struct flagcxIbQp *qp = comm->base.qps + comm->base.qpIndex;
      flagcxIbucAddEvent(req, qp->devIndex, &comm->devs[qp->devIndex].base);
      comm->base.qpIndex = (comm->base.qpIndex + 1) % comm->base.nqps;
    }
  }

  TIME_STOP(1);

  // Post to FIFO to notify sender
  TIME_START(2);
  FLAGCXCHECK(flagcxIbucPostFifo(comm, n, data, sizes, tags, mhandles, req));
  TIME_STOP(2);

  *request = req;
  return flagcxSuccess;
}

flagcxResult_t flagcxIbucIflush(void *recvComm, int n, void **data, int *sizes,
                                void **mhandles, void **request) {
  struct flagcxIbRecvComm *comm = (struct flagcxIbRecvComm *)recvComm;
  int last = -1;
  for (int i = 0; i < n; i++)
    if (sizes[i])
      last = i;
  if (comm->flushEnabled == 0 || last == -1)
    return flagcxSuccess;

  // Only flush once using the last non-zero receive
  struct flagcxIbRequest *req;
  FLAGCXCHECK(flagcxIbucGetRequest(&comm->base, &req));
  req->type = FLAGCX_NET_IB_REQ_FLUSH;
  req->sock = &comm->base.sock;
  // struct flagcxIbMrHandle *mhandle = (struct flagcxIbMrHandle
  // *)mhandles[last];

  // We don't know which devIndex the recv was on, so we flush on all devices
  // For flush operations, we use RC QP which supports RDMA_READ
  for (int i = 0; i < comm->base.ndevs; i++) {
    struct ibv_send_wr wr;
    memset(&wr, 0, sizeof(wr));
    wr.wr_id = req - comm->base.reqs;

    // Use RDMA_READ for flush operations
    wr.wr.rdma.remote_addr = (uint64_t)data[last];
    wr.wr.rdma.rkey = ((struct flagcxIbMrHandle *)mhandles[last])->mrs[i]->rkey;
    wr.sg_list = &comm->devs[i].gpuFlush.sge;
    wr.num_sge = 1;
    wr.opcode = IBV_WR_RDMA_READ;
    wr.send_flags = IBV_SEND_SIGNALED;

    TIME_START(4);
    struct ibv_send_wr *bad_wr;
    FLAGCXCHECK(
        flagcxWrapIbvPostSend(comm->devs[i].gpuFlush.qp.qp, &wr, &bad_wr));
    TIME_STOP(4);

    flagcxIbucAddEvent(req, i, &comm->devs[i].base);
  }

  *request = req;
  return flagcxSuccess;
}

flagcxResult_t flagcxIbucTest(void *request, int *done, int *sizes) {
  struct flagcxIbRequest *r = (struct flagcxIbRequest *)request;

  if (r->type == FLAGCX_NET_IB_REQ_SEND && r->base->isSend) {
    struct flagcxIbSendComm *sComm = (struct flagcxIbSendComm *)r->base;
    if (sComm->retrans.enabled) {
      for (int i = 0; i < sComm->base.ndevs; i++) {
        for (int p = 0; p < 4; p++) {
          flagcxResult_t poll_result = flagcxIbRetransRecvAckViaUd(sComm, i);
          if (poll_result != flagcxSuccess)
            break;
        }
      }

      uint64_t now_us = flagcxIbGetTimeUs();
      const uint64_t CHECK_INTERVAL_US = 1000; // 1ms
      if (now_us - sComm->last_timeout_check_us >= CHECK_INTERVAL_US) {
        FLAGCXCHECK(flagcxIbRetransCheckTimeout(&sComm->retrans, sComm));
        sComm->last_timeout_check_us = now_us;
      }
    }
  }

  if (r->type == FLAGCX_NET_IB_REQ_RECV && !r->base->isSend) {
    struct flagcxIbRecvComm *rComm = (struct flagcxIbRecvComm *)r->base;
    if (rComm->retrans.enabled && rComm->srqMgr.srq != NULL) {
      // Post to SRQ when we have pending post requests (similar to UCCL's
      // check_srq)
      const int kPostThreshold = 16;
      if (rComm->srqMgr.postSrqCount >= kPostThreshold) {
        FLAGCXCHECK(flagcxIbSrqPostRecv(&rComm->srqMgr, FLAGCX_IB_ACK_BUF_COUNT));
      }
    }
  }

  *done = 0;
  while (1) {
    if (r->events[0] == 0 && r->events[1] == 0) {
      TRACE(FLAGCX_NET, "r=%p done", r);
      *done = 1;
      if (sizes && r->type == FLAGCX_NET_IB_REQ_RECV) {
        for (int i = 0; i < r->nreqs; i++)
          sizes[i] = r->recv.sizes[i];
      }
      if (sizes && r->type == FLAGCX_NET_IB_REQ_SEND) {
        sizes[0] = r->send.size;
      }
      FLAGCXCHECK(flagcxIbFreeRequest(r));
      return flagcxSuccess;
    }

    int totalWrDone = 0;
    int wrDone = 0;
    struct ibv_wc wcs[4];

    for (int i = 0; i < FLAGCX_IB_MAX_DEVS_PER_NIC; i++) {
      TIME_START(3);
      // If we expect any completions from this device's CQ
      if (r->events[i]) {
        FLAGCXCHECK(flagcxWrapIbvPollCq(r->devBases[i]->cq, 4, wcs, &wrDone));
        totalWrDone += wrDone;
        if (wrDone == 0) {
          TIME_CANCEL(3);
        } else {
          TIME_STOP(3);
        }
        if (wrDone == 0)
          continue;
        for (int w = 0; w < wrDone; w++) {
          struct ibv_wc *wc = wcs + w;
          if (wc->status != IBV_WC_SUCCESS) {
            union flagcxSocketAddress addr;
            flagcxSocketGetAddr(r->sock, &addr);
            char localGidString[INET6_ADDRSTRLEN] = "";
            char remoteGidString[INET6_ADDRSTRLEN] = "";
            const char *localGidStr = NULL, *remoteGidStr = NULL;
            if (r->devBases[i]->gidInfo.link_layer == IBV_LINK_LAYER_ETHERNET) {
              localGidStr =
                  inet_ntop(AF_INET6, &r->devBases[i]->gidInfo.localGid,
                            localGidString, sizeof(localGidString));
              remoteGidStr =
                  inet_ntop(AF_INET6, &r->base->remDevs[i].remoteGid,
                            remoteGidString, sizeof(remoteGidString));
            }

            char line[SOCKET_NAME_MAXLEN + 1];
            WARN("NET/IBUC : Got completion from peer %s with status=%d "
                 "opcode=%d len=%d vendor err %d (%s)%s%s%s%s",
                 flagcxSocketToString(&addr, line), wc->status, wc->opcode,
                 wc->byte_len, wc->vendor_err, reqTypeStr[r->type],
                 localGidStr ? " localGid " : "", localGidString,
                 remoteGidStr ? " remoteGids" : "", remoteGidString);
            return flagcxRemoteError;
          }

          union flagcxSocketAddress addr;
          flagcxSocketGetAddr(r->sock, &addr);

          if (wc->wr_id == FLAGCX_RETRANS_WR_ID) {
            if (r->base->isSend) {
              struct flagcxIbSendComm *sComm =
                  (struct flagcxIbSendComm *)r->base;
              sComm->outstanding_retrans--;
              TRACE(FLAGCX_NET,
                    "SEND retrans completed, outstanding_retrans=%d",
                    sComm->outstanding_retrans);
            }
            continue;
          }

          // Handle SRQ completions (similar to UCCL)
          // When using SRQ, wr_id is buffer index, not req_idx
          if (!r->base->isSend) {
            struct flagcxIbRecvComm *rComm = (struct flagcxIbRecvComm *)r->base;

            if (rComm->retrans.enabled && rComm->srqMgr.srq != NULL) {
              // SRQ mode: wr_id is buffer index
              int buf_idx = (int)wc->wr_id;

              if (buf_idx < 0 || buf_idx >= rComm->srqMgr.bufCount) {
                WARN("SRQ completion with invalid buffer index: %d (max=%d)",
                     buf_idx, rComm->srqMgr.bufCount);
                continue;
              }

              void *chunk_addr = rComm->srqMgr.bufs[buf_idx].buffer;

              if (wc->opcode == IBV_WC_RECV) {
                // This is a SEND retransmission
                struct flagcxIbRetransHdr *hdr =
                    (struct flagcxIbRetransHdr *)chunk_addr;
                if (hdr->magic == FLAGCX_RETRANS_MAGIC) {
                  uint32_t seq = hdr->seq;

                  struct flagcxIbAckMsg ack_msg = {0};
                  int should_ack = 0;
                  FLAGCXCHECK(flagcxIbRetransRecvPacket(&rComm->retrans, seq,
                                                        &ack_msg, &should_ack));

                  if (should_ack) {
                    FLAGCXCHECK(
                        flagcxIbRetransSendAckViaUd(rComm, &ack_msg, 0));
                  }

                  TRACE(FLAGCX_NET,
                        "Received SEND retransmission from SRQ: seq=%u", seq);
                }

                // Return buffer and inc post counter
                rComm->srqMgr.bufs[buf_idx].inUse = 0;
                rComm->srqMgr.freeBufIndices[rComm->srqMgr.freeBufCount] =
                    buf_idx;
                rComm->srqMgr.freeBufCount++;
                rComm->srqMgr.postSrqCount++;
                continue;
              }

              if (wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
                // This is normal data from SRQ (similar to UCCL uc_rx_chunk)
                if (r->type == FLAGCX_NET_IB_REQ_RECV && r->nreqs == 1) {
                  if (rComm->retrans.enabled &&
                      rComm->devs[0].ctrlQp.qp != NULL) {
                    uint32_t seq, size;
                    flagcxIbDecodeImmData(wc->imm_data, &seq, &size);
                    r->recv.sizes[0] = size;

                    struct flagcxIbAckMsg ack_msg = {0};
                    int should_ack = 0;
                    FLAGCXCHECK(flagcxIbRetransRecvPacket(
                        &rComm->retrans, seq, &ack_msg, &should_ack));

                    if (should_ack) {
                      FLAGCXCHECK(
                          flagcxIbRetransSendAckViaUd(rComm, &ack_msg, 0));
                    }
                  } else {
                    r->recv.sizes[0] = wc->imm_data;
                  }
                }

                // Return buffer and inc post counter
                rComm->srqMgr.bufs[buf_idx].inUse = 0;
                rComm->srqMgr.freeBufIndices[rComm->srqMgr.freeBufCount] =
                    buf_idx;
                rComm->srqMgr.freeBufCount++;
                rComm->srqMgr.postSrqCount++;

                // Decrement events for the current request
                r->events[i]--;
                continue;
              }
            }
          }

          uint8_t req_idx = wc->wr_id & 0xff;

          if (req_idx >= MAX_REQUESTS) {
            continue;
          }

          struct flagcxIbRequest *req = r->base->reqs + req_idx;

#ifdef ENABLE_TRACE
          char line[SOCKET_NAME_MAXLEN + 1];
          TRACE(FLAGCX_NET,
                "Got completion from peer %s with status=%d opcode=%d len=%d "
                "wr_id=%ld r=%p type=%d events={%d,%d}, i=%d",
                flagcxSocketToString(&addr, line), wc->status, wc->opcode,
                wc->byte_len, wc->wr_id, req, req->type, req->events[0],
                req->events[1], i);
#endif
          if (req->type == FLAGCX_NET_IB_REQ_SEND) {
            for (int j = 0; j < req->nreqs; j++) {
              struct flagcxIbRequest *sendReq =
                  r->base->reqs + ((wc->wr_id >> (j * 8)) & 0xff);
              if ((sendReq->events[i] <= 0)) {
                WARN("NET/IBUC: sendReq(%p)->events={%d,%d}, i=%d, j=%d <= 0",
                     sendReq, sendReq->events[0], sendReq->events[1], i, j);
                return flagcxInternalError;
              }
              sendReq->events[i]--;
            }

            struct flagcxIbSendComm *sComm =
                (struct flagcxIbSendComm *)req->base;
            if (sComm->outstanding_sends > 0) {
              sComm->outstanding_sends--;
            }
          } else {
            if (req && wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
              if (req->type != FLAGCX_NET_IB_REQ_RECV) {
                static __thread int type_mismatch_count = 0;
                if ((type_mismatch_count++ % 100) == 0) {
                  WARN("NET/IBUC: Type mismatch for RECV_RDMA_WITH_IMM: "
                       "req->type=%d (expected RECV=%d), count=%d",
                       req->type, FLAGCX_NET_IB_REQ_RECV, type_mismatch_count);
                }
                continue;
              }

              struct flagcxIbRecvComm *rComm =
                  (struct flagcxIbRecvComm *)req->base;

              if (req->nreqs == 1) {
                if (rComm->retrans.enabled &&
                    rComm->devs[0].ctrlQp.qp != NULL) {
                  uint32_t seq, size;
                  flagcxIbDecodeImmData(wc->imm_data, &seq, &size);
                  req->recv.sizes[0] = size;

                  TRACE(FLAGCX_NET,
                        "Received packet: seq=%u, size=%u, immData=0x%x", seq,
                        size, wc->imm_data);

                  struct flagcxIbAckMsg ack_msg = {0};
                  int should_ack = 0;
                  FLAGCXCHECK(flagcxIbRetransRecvPacket(&rComm->retrans, seq,
                                                        &ack_msg, &should_ack));

                  if (should_ack) {
                    flagcxResult_t ack_result =
                        flagcxIbRetransSendAckViaUd(rComm, &ack_msg, 0);
                    if (ack_result != flagcxSuccess) {
                      TRACE(FLAGCX_NET,
                            "Failed to send ACK for seq=%u (result=%d)", seq,
                            ack_result);
                    } else {
                      TRACE(FLAGCX_NET, "Sent ACK for seq=%u, ack_seq=%u", seq,
                            ack_msg.ackSeq);
                    }
                  } else {
                    TRACE(FLAGCX_NET, "No ACK needed for seq=%u (expect=%u)",
                          seq, rComm->retrans.recvSeq);
                  }
                } else {
                  req->recv.sizes[0] = wc->imm_data;
                }
              }
            }
            req->events[i]--;
          }
        }
      }
    }

    // If no CQEs found on any device, return and come back later
    if (totalWrDone == 0)
      return flagcxSuccess;
  }
}

flagcxResult_t flagcxIbucCloseSend(void *sendComm) {
  struct flagcxIbSendComm *comm = (struct flagcxIbSendComm *)sendComm;
  if (comm) {
    if (comm->retrans.enabled) {
      INFO(FLAGCX_INIT | FLAGCX_NET,
           "Sender Retrans Stats: Sent=%lu, Retrans=%lu, Acked=%lu, "
           "Timeout=%lu, Pending=%d",
           (unsigned long)comm->retrans.totalSent,
           (unsigned long)comm->retrans.totalRetrans,
           (unsigned long)comm->retrans.totalAcked,
           (unsigned long)comm->retrans.totalTimeout,
           comm->retrans.bufferCount);
      FLAGCXCHECK(flagcxIbRetransDestroy(&comm->retrans));
    }

    FLAGCXCHECK(flagcxSocketClose(&comm->base.sock));

    for (int q = 0; q < comm->base.nqps; q++)
      if (comm->base.qps[q].qp != NULL)
        FLAGCXCHECK(flagcxWrapIbvDestroyQp(comm->base.qps[q].qp));

    for (int i = 0; i < comm->base.ndevs; i++) {
      struct flagcxIbSendCommDev *commDev = comm->devs + i;

      if (comm->retrans.enabled) {
        FLAGCXCHECK(flagcxIbDestroyCtrlQp(&commDev->ctrlQp));
        if (commDev->ackMr != NULL)
          FLAGCXCHECK(flagcxWrapIbvDeregMr(commDev->ackMr));
        if (commDev->ackBuffer != NULL)
          free(commDev->ackBuffer);
      }

      if (i == 0 && comm->retrans_hdr_mr != NULL) {
        FLAGCXCHECK(flagcxWrapIbvDeregMr(comm->retrans_hdr_mr));
      }

      if (commDev->fifoMr != NULL)
        FLAGCXCHECK(flagcxWrapIbvDeregMr(commDev->fifoMr));
      if (comm->remSizesFifo.mrs[i] != NULL)
        FLAGCXCHECK(flagcxWrapIbvDeregMr(comm->remSizesFifo.mrs[i]));
      FLAGCXCHECK(flagcxIbucDestroyBase(&commDev->base));
    }
    free(comm);
  }
  TIME_PRINT("IBUC");
  return flagcxSuccess;
}

flagcxResult_t flagcxIbucCloseRecv(void *recvComm) {
  struct flagcxIbRecvComm *comm = (struct flagcxIbRecvComm *)recvComm;
  if (comm) {
    if (comm->retrans.enabled) {
      INFO(FLAGCX_INIT | FLAGCX_NET,
           "Receiver Retrans Stats: recv_seq=%u, Sent=%lu, Retrans=%lu, "
           "Acked=%lu",
           comm->retrans.recvSeq, (unsigned long)comm->retrans.totalSent,
           (unsigned long)comm->retrans.totalRetrans,
           (unsigned long)comm->retrans.totalAcked);
      FLAGCXCHECK(flagcxIbRetransDestroy(&comm->retrans));
    }

    FLAGCXCHECK(flagcxSocketClose(&comm->base.sock));

    // Destroy QPs first, before destroying SRQ (avoid "Device or resource
    // busy")
    for (int q = 0; q < comm->base.nqps; q++)
      if (comm->base.qps[q].qp != NULL)
        FLAGCXCHECK(flagcxWrapIbvDestroyQp(comm->base.qps[q].qp));

    if (comm->srqMgr.srq != NULL) {
      FLAGCXCHECK(flagcxIbDestroySrq(&comm->srqMgr));
      TRACE(FLAGCX_NET, "Receiver: Destroyed SRQ");
    }

    for (int i = 0; i < comm->base.ndevs; i++) {
      struct flagcxIbRecvCommDev *commDev = comm->devs + i;

      if (comm->retrans.enabled) {
        FLAGCXCHECK(flagcxIbDestroyCtrlQp(&commDev->ctrlQp));
        if (commDev->ackMr != NULL)
          FLAGCXCHECK(flagcxWrapIbvDeregMr(commDev->ackMr));
        if (commDev->ackBuffer != NULL)
          free(commDev->ackBuffer);
      }

      if (comm->flushEnabled) {
        if (commDev->gpuFlush.qp.qp != NULL)
          FLAGCXCHECK(flagcxWrapIbvDestroyQp(commDev->gpuFlush.qp.qp));
        if (commDev->gpuFlush.hostMr != NULL)
          FLAGCXCHECK(flagcxWrapIbvDeregMr(commDev->gpuFlush.hostMr));
      }
      if (commDev->fifoMr != NULL)
        FLAGCXCHECK(flagcxWrapIbvDeregMr(commDev->fifoMr));
      if (commDev->sizesFifoMr != NULL)
        FLAGCXCHECK(flagcxWrapIbvDeregMr(commDev->sizesFifoMr));
      FLAGCXCHECK(flagcxIbucDestroyBase(&commDev->base));
    }
    free(comm);
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxIbucCloseListen(void *listenComm) {
  struct flagcxIbListenComm *comm = (struct flagcxIbListenComm *)listenComm;
  if (comm) {
    FLAGCXCHECK(flagcxSocketClose(&comm->sock));
    free(comm);
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxIbucGetDevFromName(char *name, int *dev) {
  for (int i = 0; i < flagcxNMergedIbDevs; i++) {
    if (strcmp(flagcxIbMergedDevs[i].devName, name) == 0) {
      *dev = i;
      return flagcxSuccess;
    }
  }
  return flagcxSystemError;
}

flagcxResult_t flagcxIbucGetProperties(int dev, void *props) {
  struct flagcxIbMergedDev *mergedDev = flagcxIbMergedDevs + dev;
  flagcxNetProperties_t *properties = (flagcxNetProperties_t *)props;

  properties->name = mergedDev->devName;
  properties->speed = mergedDev->speed;

  // Take the rest of the properties from an arbitrary sub-device (should be the
  // same)
  struct flagcxIbDev *ibucDev = flagcxIbDevs + mergedDev->devs[0];
  properties->pciPath = ibucDev->pciPath;
  properties->guid = ibucDev->guid;
  properties->ptrSupport = FLAGCX_PTR_HOST;

  if (flagcxIbGdrSupport() == flagcxSuccess) {
    properties->ptrSupport |= FLAGCX_PTR_CUDA; // GDR support via nv_peermem
  }
  properties->regIsGlobal = 1;
  if (flagcxIbDmaBufSupport(dev) == flagcxSuccess) {
    properties->ptrSupport |= FLAGCX_PTR_DMABUF;
  }
  properties->latency = 0; // Not set
  properties->port = ibucDev->portNum + ibucDev->realPort;
  properties->maxComms = ibucDev->maxQp;
  properties->maxRecvs = FLAGCX_NET_IB_MAX_RECVS;
  properties->netDeviceType = FLAGCX_NET_DEVICE_HOST;
  properties->netDeviceVersion = FLAGCX_NET_DEVICE_INVALID_VERSION;
  return flagcxSuccess;
}

// Adapter wrapper functions

struct flagcxNetAdaptor flagcxNetIbuc = {
    // Basic functions
    "IBUC", flagcxIbucInit, flagcxIbDevices, flagcxIbucGetProperties,
    NULL, // reduceSupport
    NULL, // getDeviceMr
    NULL, // irecvConsumed

    // Setup functions
    flagcxIbucListen, flagcxIbucConnect, flagcxIbucAccept, flagcxIbucCloseSend,
    flagcxIbucCloseRecv, flagcxIbucCloseListen,

    // Memory region functions
    flagcxIbucRegMr, flagcxIbucRegMrDmaBuf, flagcxIbucDeregMr,

    // Two-sided functions
    flagcxIbucIsend, flagcxIbucIrecv, flagcxIbucIflush, flagcxIbucTest,

    // One-sided functions
    NULL, // write
    NULL, // read
    NULL, // signal

    // Device name lookup
    flagcxIbucGetDevFromName};

#endif // USE_IBUC
