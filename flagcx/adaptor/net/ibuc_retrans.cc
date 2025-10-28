/*************************************************************************
 * Copyright (c) 2023 BAAI. All rights reserved.
 * Copyright (c) 2024, UCCL Project. All rights reserved.
 *
 * IBUC Retransmission Support - Implementation
 ************************************************************************/

#include "ibuc_retrans.h"
#include "flagcx_common.h"
#include "ibvcore.h"
#include "ibvwrap.h"
#include "param.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>

FLAGCX_PARAM(IbRetransEnable, "IB_RETRANS_ENABLE", 0);
FLAGCX_PARAM(IbRetransTimeout, "IB_RETRANS_TIMEOUT", 5000); // 5ms (was 500us)
FLAGCX_PARAM(IbRetransMaxRetry, "IB_RETRANS_MAX_RETRY", 10);
FLAGCX_PARAM(IbRetransAckInterval, "IB_RETRANS_ACK_INTERVAL", 16); // 16 (was 8)
FLAGCX_PARAM(IbMaxOutstanding, "IB_MAX_OUTSTANDING",
             128); // QP depth is 512, allow more for large messages

flagcxResult_t flagcxIbRetransInit(struct flagcxIbRetransState *state) {
  if (!state)
    return flagcxInternalError;

  memset(state, 0, sizeof(struct flagcxIbRetransState));

  state->enabled = flagcxParamIbRetransEnable();
  state->maxRetry = flagcxParamIbRetransMaxRetry();
  state->ackInterval = flagcxParamIbRetransAckInterval();
  state->minRtoUs = flagcxParamIbRetransTimeout();
  state->maxRtoUs = state->minRtoUs * 64;

  TRACE(FLAGCX_NET,
        "flagcxIbRetransInit: enabled=%d, max_retry=%d, ack_interval=%d, "
        "min_rto=%luus",
        state->enabled, state->maxRetry, state->ackInterval,
        (unsigned long)state->minRtoUs);

  state->rtoUs = state->minRtoUs;
  state->srttUs = 0;
  state->rttvarUs = 0;

  state->sendSeq = 0;
  state->sendUna = 0;
  state->recvSeq = 0;

  state->bufferHead = 0;
  state->bufferTail = 0;
  state->bufferCount = 0;

  state->lastAckTimeUs = flagcxIbGetTimeUs();

  if (state->enabled) {
    INFO(FLAGCX_INIT | FLAGCX_NET,
         "IBUC Retransmission enabled: RTO=%uus, MaxRetry=%d, AckInterval=%d",
         state->minRtoUs, state->maxRetry, state->ackInterval);
  }

  return flagcxSuccess;
}

flagcxResult_t flagcxIbRetransDestroy(struct flagcxIbRetransState *state) {
  if (!state)
    return flagcxInternalError;

  if (state->enabled) {
    flagcxIbRetransPrintStats(state, "Final");
  }

  return flagcxSuccess;
}

flagcxResult_t flagcxIbRetransAddPacket(struct flagcxIbRetransState *state,
                                        uint32_t seq, uint32_t size, void *data,
                                        uint64_t remote_addr, uint32_t *lkeys,
                                        uint32_t *rkeys) {

  if (!state || !state->enabled)
    return flagcxSuccess;

  if (state->bufferCount >= FLAGCX_IB_RETRANS_MAX_INFLIGHT) {
    static __thread int warn_count = 0;
    if (warn_count++ < 5) {
      WARN("Retransmission buffer full: count=%d, max=%d. "
           "send_seq=%u, send_una=%u, recv_seq=%u, total_sent=%lu, "
           "total_acked=%lu",
           state->bufferCount, FLAGCX_IB_RETRANS_MAX_INFLIGHT, state->sendSeq,
           state->sendUna, state->recvSeq, (unsigned long)state->totalSent,
           (unsigned long)state->totalAcked);
      WARN("This usually means ACK is not being received properly.");
      WARN("Dropping oldest packet to avoid deadlock (seq=%u)",
           state->buffer[state->bufferHead].seq);
    }

    state->buffer[state->bufferHead].valid = 0;
    state->bufferHead =
        (state->bufferHead + 1) % FLAGCX_IB_RETRANS_MAX_INFLIGHT;
    state->bufferCount--;
  }

  if (state->bufferCount > FLAGCX_IB_RETRANS_MAX_INFLIGHT * 3 / 4) {
    static __thread uint64_t last_warn_time = 0;
    uint64_t now = flagcxIbGetTimeUs();
    if (now - last_warn_time > 1000000) {
      WARN("Retransmission buffer %d%% full (%d/%d). ACK may not be working "
           "properly.",
           (state->bufferCount * 100) / FLAGCX_IB_RETRANS_MAX_INFLIGHT,
           state->bufferCount, FLAGCX_IB_RETRANS_MAX_INFLIGHT);
      last_warn_time = now;
    }
  }

  int idx = state->bufferTail;
  struct flagcxIbRetransEntry *entry = &state->buffer[idx];

  entry->seq = seq;
  entry->size = size;
  entry->sendTimeUs = flagcxIbGetTimeUs();
  entry->remoteAddr = remote_addr;
  entry->data = data;

  if (lkeys) {
    memcpy(entry->lkeys, lkeys, sizeof(entry->lkeys));
  }
  if (rkeys) {
    memcpy(entry->rkeys, rkeys, sizeof(entry->rkeys));
  }

  entry->retry_count = 0;
  entry->valid = 1;

  state->bufferTail =
      (state->bufferTail + 1) % FLAGCX_IB_RETRANS_MAX_INFLIGHT;
  state->bufferCount++;
  state->totalSent++;

  TRACE(FLAGCX_NET, "Added packet to retrans buffer: seq=%u, size=%u, count=%d",
        seq, size, state->bufferCount);

  return flagcxSuccess;
}

static void flagcxIbUpdateRTO(struct flagcxIbRetransState *state,
                              uint64_t rtt_us) {
  if (state->srttUs == 0) {
    state->srttUs = rtt_us;
    state->rttvarUs = rtt_us / 2;
  } else {
    // RFC 6298: SRTT = (1 - alpha) * SRTT + alpha * RTT
    // RTTVAR = (1 - beta) * RTTVAR + beta * |SRTT - RTT|
    // alpha = 1/8, beta = 1/4
    uint64_t delta = (rtt_us > state->srttUs) ? (rtt_us - state->srttUs)
                                               : (state->srttUs - rtt_us);
    state->rttvarUs = (3 * state->rttvarUs + delta) / 4;
    state->srttUs = (7 * state->srttUs + rtt_us) / 8;
  }

  state->rtoUs = state->srttUs + 4 * state->rttvarUs;

  if (state->rtoUs < state->minRtoUs) {
    state->rtoUs = state->minRtoUs;
  }
  if (state->rtoUs > state->maxRtoUs) {
    state->rtoUs = state->maxRtoUs;
  }

  TRACE(FLAGCX_NET,
        "Updated RTO: RTT=%luus, SRTT=%luus, RTTVAR=%luus, RTO=%luus",
        (unsigned long)rtt_us, (unsigned long)state->srttUs,
        (unsigned long)state->rttvarUs, (unsigned long)state->rtoUs);
}

flagcxResult_t flagcxIbRetransProcessAck(struct flagcxIbRetransState *state,
                                         struct flagcxIbAckMsg *ack_msg) {

  if (!state || !state->enabled || !ack_msg)
    return flagcxSuccess;

  uint32_t ack_seq = ack_msg->ackSeq;
  uint64_t now_us = flagcxIbGetTimeUs();

  TRACE(FLAGCX_NET, "Processing ACK: ack_seq=%u, send_una=%u, buffer_count=%d",
        ack_seq, state->sendUna, state->bufferCount);

  if (ack_msg->timestampUs > 0) {
    uint64_t rtt_us = now_us - ack_msg->timestampUs;
    if (rtt_us < 10000000) {
      flagcxIbUpdateRTO(state, rtt_us);
    }
  }

  state->lastAckTimeUs = now_us;

  int freed = 0;
  while (state->bufferCount > 0) {
    struct flagcxIbRetransEntry *entry = &state->buffer[state->bufferHead];

    if (!entry->valid) {
      state->bufferHead =
          (state->bufferHead + 1) % FLAGCX_IB_RETRANS_MAX_INFLIGHT;
      state->bufferCount--;
    } else if (flagcxIbSeqLeq(entry->seq, ack_seq)) {
      entry->valid = 0;
      state->bufferHead =
          (state->bufferHead + 1) % FLAGCX_IB_RETRANS_MAX_INFLIGHT;
      state->bufferCount--;
      state->totalAcked++;
      freed++;

      state->sendUna = entry->seq + 1;
    } else {
      break;
    }
  }

  uint64_t sack_bitmap = ack_msg->sackBitmap;
  uint16_t sack_count = ack_msg->sackBitmapCount;

  if (sack_bitmap != 0 && sack_count > 0) {
    TRACE(FLAGCX_NET, "Processing SACK: bitmap=0x%lx, count=%u",
          (unsigned long)sack_bitmap, sack_count);

    int idx = state->bufferHead;
    for (int i = 0; i < state->bufferCount && i < 64; i++) {
      struct flagcxIbRetransEntry *entry = &state->buffer[idx];

      if (entry->valid) {
        uint32_t entry_offset = entry->seq - state->sendUna;

        if (entry_offset < 64) {
          if (sack_bitmap & (1ULL << entry_offset)) {
            TRACE(FLAGCX_NET, "SACK confirmed packet: seq=%u", entry->seq);
            entry->valid = 0;
            state->totalAcked++;
            freed++;
          }
        }
      }

      idx = (idx + 1) % FLAGCX_IB_RETRANS_MAX_INFLIGHT;
    }

    while (state->bufferCount > 0) {
      struct flagcxIbRetransEntry *entry = &state->buffer[state->bufferHead];
      if (!entry->valid) {
        state->bufferHead =
            (state->bufferHead + 1) % FLAGCX_IB_RETRANS_MAX_INFLIGHT;
        state->bufferCount--;
      } else {
        break;
      }
    }
  }

  if (freed > 0) {
    TRACE(FLAGCX_NET,
          "ACK processed: freed %d packets, remaining=%d, ack_seq=%u", freed,
          state->bufferCount, ack_seq);
  }

  return flagcxSuccess;
}

flagcxResult_t flagcxIbRetransResendViaSend(struct flagcxIbSendComm *comm,
                                            uint32_t seq) {

  if (!comm || !comm->retrans.enabled)
    return flagcxSuccess;

  struct flagcxIbRetransEntry *entry = NULL;
  int idx = comm->retrans.bufferHead;
  for (int i = 0; i < comm->retrans.bufferCount; i++) {
    if (comm->retrans.buffer[idx].valid &&
        comm->retrans.buffer[idx].seq == seq) {
      entry = &comm->retrans.buffer[idx];
      break;
    }
    idx = (idx + 1) % FLAGCX_IB_RETRANS_MAX_INFLIGHT;
  }

  if (!entry) {
    TRACE(FLAGCX_NET, "Retrans: seq=%u not found in buffer (already acked?)",
          seq);
    return flagcxSuccess;
  }

  // Flow control: Check if we have room for retransmission
  int total_outstanding = comm->outstanding_sends + comm->outstanding_retrans;
  if (total_outstanding >= comm->max_outstanding) {
    TRACE(FLAGCX_NET,
          "Retrans deferred: outstanding=%d (sends=%d + retrans=%d) >= max=%d",
          total_outstanding, comm->outstanding_sends, comm->outstanding_retrans,
          comm->max_outstanding);
    return flagcxSuccess; // Defer retransmission, will retry later
  }

  int qpIndex = comm->retrans.retransQPIndex % comm->base.nqps;
  comm->retrans.retransQPIndex++;

  struct flagcxIbQp *qp = &comm->base.qps[qpIndex];
  int devIndex = qp->devIndex;

  struct ibv_send_wr wr;
  struct ibv_sge sge[2];
  memset(&wr, 0, sizeof(wr));

  struct flagcxIbRetransHdr *hdr = &comm->retrans_hdr_pool[seq % 32];
  hdr->magic = FLAGCX_RETRANS_MAGIC;
  hdr->seq = seq;
  hdr->size = entry->size;
  hdr->remoteAddr = entry->remoteAddr;

  sge[0].addr = (uint64_t)hdr;
  sge[0].length = sizeof(struct flagcxIbRetransHdr);
  sge[0].lkey = comm->retrans_hdr_mr->lkey;

  sge[1].addr = (uint64_t)entry->data;
  sge[1].length = entry->size;
  sge[1].lkey = entry->lkeys[devIndex];

  wr.wr_id = FLAGCX_RETRANS_WR_ID;
  wr.sg_list = sge;
  wr.num_sge = 2;
  wr.opcode = IBV_WR_SEND;
  wr.send_flags = IBV_SEND_SIGNALED;
  wr.next = NULL;

  struct ibv_send_wr *bad_wr;
  flagcxResult_t result = flagcxWrapIbvPostSend(qp->qp, &wr, &bad_wr);

  if (result != flagcxSuccess) {
    TRACE(FLAGCX_NET, "Retrans SEND failed for seq=%u, qp=%d", seq, qpIndex);
    return result;
  }

  entry->retry_count++;
  entry->sendTimeUs = flagcxIbGetTimeUs();
  comm->retrans.totalRetrans++;
  comm->outstanding_retrans++;

  TRACE(FLAGCX_NET,
        "Retransmitted via SEND: seq=%u, size=%u, qp=%d, devIndex=%d, "
        "retry_count=%d",
        seq, entry->size, qpIndex, devIndex, entry->retry_count);

  return flagcxSuccess;
}

flagcxResult_t flagcxIbRetransCheckTimeout(struct flagcxIbRetransState *state,
                                           struct flagcxIbSendComm *comm) {

  if (!state || !state->enabled || !comm)
    return flagcxSuccess;
  if (state->bufferCount == 0)
    return flagcxSuccess;

  uint64_t now_us = flagcxIbGetTimeUs();
  int retrans_count = 0;
  const int MAX_RETRANS_PER_CALL = 1;

  int idx = state->bufferHead;
  for (int i = 0;
       i < state->bufferCount && retrans_count < MAX_RETRANS_PER_CALL; i++) {
    struct flagcxIbRetransEntry *entry = &state->buffer[idx];

    if (entry->valid) {
      uint64_t elapsed_us = now_us - entry->sendTimeUs;

      if (elapsed_us >= state->rtoUs) {
        if (entry->retry_count >= state->maxRetry) {
          WARN("Packet exceeded max retries: seq=%u, retry=%d, max=%d. "
               "Transmission failed, aborting operation.",
               entry->seq, entry->retry_count, state->maxRetry);
          return flagcxRemoteError;
        }

        flagcxResult_t retrans_result =
            flagcxIbRetransResendViaSend(comm, entry->seq);
        if (retrans_result == flagcxSuccess) {
          state->totalTimeout++;
          retrans_count++;

          state->rtoUs = (state->rtoUs * 2 > state->maxRtoUs)
                              ? state->maxRtoUs
                              : state->rtoUs * 2;
        } else {
          TRACE(FLAGCX_NET,
                "Retransmit failed, stopping this round to allow queue drain");
          break;
        }
      }
    }

    idx = (idx + 1) % FLAGCX_IB_RETRANS_MAX_INFLIGHT;
  }

  if (retrans_count > 0) {
    TRACE(FLAGCX_NET, "Retransmitted %d packets, RTO=%luus, pending=%d",
          retrans_count, (unsigned long)state->rtoUs, state->bufferCount);
  }

  return flagcxSuccess;
}

flagcxResult_t flagcxIbRetransRecvPacket(struct flagcxIbRetransState *state,
                                         uint32_t seq,
                                         struct flagcxIbAckMsg *ack_msg,
                                         int *should_ack) {

  if (!state || !state->enabled || !ack_msg || !should_ack) {
    *should_ack = 0;
    return flagcxSuccess;
  }

  *should_ack = 0;
  uint64_t now_us = flagcxIbGetTimeUs();

  TRACE(FLAGCX_NET, "Received packet: seq=%u, expect=%u", seq, state->recvSeq);

  if (seq == state->recvSeq) {
    state->recvSeq = (state->recvSeq + 1) & 0xFFFF;

    // Send ACK periodically based on recv_seq to avoid excessive ACK traffic
    if ((state->recvSeq % state->ackInterval) == 0) {
      *should_ack = 1;
    }
  } else if (flagcxIbSeqLess(seq, state->recvSeq)) {
    TRACE(FLAGCX_NET, "Received duplicate packet: seq=%u, expect=%u", seq,
          state->recvSeq);
    *should_ack = 1;
  } else {
    TRACE(FLAGCX_NET, "Received out-of-order packet: seq=%u, expect=%u", seq,
          state->recvSeq);
    *should_ack = 1;

    int gap = seq - state->recvSeq;
    if (gap > 0 && gap < 64) {
      ack_msg->sackBitmap |= (1ULL << (gap - 1));

      ack_msg->sackBitmapCount = 0;
      uint64_t bitmap = ack_msg->sackBitmap;
      while (bitmap) {
        ack_msg->sackBitmapCount += (bitmap & 1);
        bitmap >>= 1;
      }

      TRACE(FLAGCX_NET, "SACK: gap=%d, bitmap=0x%lx, count=%u", gap,
            (unsigned long)ack_msg->sackBitmap, ack_msg->sackBitmapCount);
    }
  }

  if (*should_ack) {
    ack_msg->ackSeq = (state->recvSeq - 1) & 0xFFFF;
    ack_msg->timestampUs = now_us;
    ack_msg->peerId = 0;
    ack_msg->flowId = 0;
    ack_msg->path = 0;

    TRACE(FLAGCX_NET,
          "Generated ACK: ack_seq=%u, sack_count=%u, sack_bitmap=0x%lx",
          ack_msg->ackSeq, ack_msg->sackBitmapCount,
          (unsigned long)ack_msg->sackBitmap);
  }

  return flagcxSuccess;
}

flagcxResult_t flagcxIbRetransPiggybackAck(struct flagcxIbSendFifo *fifo_elem,
                                           struct flagcxIbAckMsg *ack_msg) {
  return flagcxSuccess;
}

flagcxResult_t flagcxIbRetransExtractAck(struct flagcxIbSendFifo *fifo_elem,
                                         struct flagcxIbAckMsg *ack_msg) {
  return flagcxSuccess;
}

void flagcxIbRetransPrintStats(struct flagcxIbRetransState *state,
                               const char *prefix) {
  if (!state || !state->enabled)
    return;

  INFO(
      FLAGCX_NET,
      "%s Retransmission Stats: Sent=%lu, Retrans=%lu, Acked=%lu, Timeout=%lu, "
      "Pending=%d, RTO=%luus, SRTT=%luus",
      prefix ? prefix : "", (unsigned long)state->totalSent,
      (unsigned long)state->totalRetrans, (unsigned long)state->totalAcked,
      (unsigned long)state->totalTimeout, state->bufferCount,
      (unsigned long)state->rtoUs, (unsigned long)state->srttUs);
}

flagcxResult_t flagcxIbCreateCtrlQp(struct ibv_context *context,
                                    struct ibv_pd *pd, uint8_t port_num,
                                    struct flagcxIbCtrlQp *ctrlQp) {

  if (!context || !pd || !ctrlQp)
    return flagcxInternalError;

  memset(ctrlQp, 0, sizeof(struct flagcxIbCtrlQp));

  FLAGCXCHECK(flagcxWrapIbvCreateCq(&ctrlQp->cq, context, 1024, NULL, NULL, 0));

  struct ibv_qp_init_attr qp_init_attr;
  memset(&qp_init_attr, 0, sizeof(qp_init_attr));
  qp_init_attr.qp_type = IBV_QPT_UD;
  qp_init_attr.send_cq = ctrlQp->cq;
  qp_init_attr.recv_cq = ctrlQp->cq;
  qp_init_attr.cap.max_send_wr = 512;
  qp_init_attr.cap.max_recv_wr = 128;
  qp_init_attr.cap.max_send_sge = 1;
  qp_init_attr.cap.max_recv_sge = 1;
  qp_init_attr.cap.max_inline_data = 64;

  FLAGCXCHECK(flagcxWrapIbvCreateQp(&ctrlQp->qp, pd, &qp_init_attr));
  if (!ctrlQp->qp) {
    WARN("Failed to create control UD QP");
    flagcxWrapIbvDestroyCq(ctrlQp->cq);
    return flagcxInternalError;
  }

  struct ibv_qp_attr qp_attr;
  memset(&qp_attr, 0, sizeof(qp_attr));
  qp_attr.qp_state = IBV_QPS_INIT;
  qp_attr.pkey_index = 0;
  qp_attr.port_num = port_num;
  qp_attr.qkey = 0x11111111;

  FLAGCXCHECK(flagcxWrapIbvModifyQp(ctrlQp->qp, &qp_attr,
                                    IBV_QP_STATE | IBV_QP_PKEY_INDEX |
                                        IBV_QP_PORT | IBV_QP_QKEY));

  TRACE(FLAGCX_NET, "Created control UD QP: qpn=%u", ctrlQp->qp->qp_num);

  return flagcxSuccess;
}

flagcxResult_t flagcxIbDestroyCtrlQp(struct flagcxIbCtrlQp *ctrlQp) {
  if (!ctrlQp)
    return flagcxSuccess;

  if (ctrlQp->ah) {
    if (ctrlQp->qp && ctrlQp->qp->context) {
      ctrlQp->qp->context->ops.destroy_ah(ctrlQp->ah);
    }
    ctrlQp->ah = NULL;
  }

  if (ctrlQp->qp) {
    FLAGCXCHECK(flagcxWrapIbvDestroyQp(ctrlQp->qp));
    ctrlQp->qp = NULL;
  }

  if (ctrlQp->cq) {
    FLAGCXCHECK(flagcxWrapIbvDestroyCq(ctrlQp->cq));
    ctrlQp->cq = NULL;
  }

  return flagcxSuccess;
}

flagcxResult_t
flagcxIbSetupCtrlQpConnection(struct ibv_context *context, struct ibv_pd *pd,
                              struct flagcxIbCtrlQp *ctrlQp,
                              uint32_t remote_qpn, union ibv_gid *remote_gid,
                              uint16_t remote_lid, uint8_t port_num,
                              uint8_t link_layer, uint8_t local_gid_index) {

  if (!ctrlQp || !ctrlQp->qp)
    return flagcxInternalError;

  ctrlQp->remoteQpn = remote_qpn;
  ctrlQp->remoteQkey = 0x11111111;

  struct ibv_qp_attr qp_attr;
  memset(&qp_attr, 0, sizeof(qp_attr));
  qp_attr.qp_state = IBV_QPS_RTR;

  FLAGCXCHECK(flagcxWrapIbvModifyQp(ctrlQp->qp, &qp_attr, IBV_QP_STATE));

  memset(&qp_attr, 0, sizeof(qp_attr));
  qp_attr.qp_state = IBV_QPS_RTS;
  qp_attr.sq_psn = 0;

  FLAGCXCHECK(flagcxWrapIbvModifyQp(ctrlQp->qp, &qp_attr,
                                    IBV_QP_STATE | IBV_QP_SQ_PSN));

  struct ibv_ah_attr ah_attr;
  memset(&ah_attr, 0, sizeof(ah_attr));
  ah_attr.port_num = port_num;

  if (link_layer == IBV_LINK_LAYER_ETHERNET) {
    if (!remote_gid) {
      WARN("remote_gid is NULL for RoCE");
      return flagcxInternalError;
    }

    TRACE(FLAGCX_NET,
          "Creating AH for RoCE: remote_gid=%lx:%lx, local_gid_idx=%u, port=%u",
          (unsigned long)remote_gid->global.subnet_prefix,
          (unsigned long)remote_gid->global.interface_id, local_gid_index,
          port_num);

    ah_attr.is_global = 1;
    ah_attr.grh.dgid = *remote_gid;
    ah_attr.grh.sgid_index = local_gid_index;
    ah_attr.grh.hop_limit = 255;
    ah_attr.grh.traffic_class = 0;
    ah_attr.grh.flow_label = 0;
  } else {
    TRACE(FLAGCX_NET, "Creating AH for IB: remote_lid=%u, port=%u", remote_lid,
          port_num);

    ah_attr.is_global = 0;
    ah_attr.dlid = remote_lid;
  }

  ah_attr.sl = 0;
  ah_attr.src_path_bits = 0;

  ctrlQp->ah = context->ops.create_ah(pd, &ah_attr);
  if (!ctrlQp->ah) {
    WARN("  link_layer=%d (%s)", link_layer,
         link_layer == IBV_LINK_LAYER_ETHERNET ? "RoCE" : "IB");
    WARN("  remote_lid=%u", remote_lid);
    if (link_layer == IBV_LINK_LAYER_ETHERNET && remote_gid) {
      WARN("  remote_gid=%lx:%lx",
           (unsigned long)remote_gid->global.subnet_prefix,
           (unsigned long)remote_gid->global.interface_id);
      WARN("  local_gid_index=%u", local_gid_index);
    }
    return flagcxSuccess;
  }

  INFO(FLAGCX_NET, "Control QP setup: local_qpn=%u, remote_qpn=%u",
       ctrlQp->qp->qp_num, remote_qpn);

  return flagcxSuccess;
}

flagcxResult_t flagcxIbRetransSendAckViaUd(struct flagcxIbRecvComm *comm,
                                           struct flagcxIbAckMsg *ack_msg,
                                           int devIndex) {

  if (!comm || !ack_msg || devIndex >= comm->base.ndevs) {
    WARN("Invalid parameters for sending ACK: comm=%p, ack_msg=%p, "
         "devIndex=%d, ndevs=%d",
         comm, ack_msg, devIndex, comm ? comm->base.ndevs : -1);
    return flagcxInternalError;
  }

  struct flagcxIbRecvCommDev *commDev = &comm->devs[devIndex];
  struct flagcxIbCtrlQp *ctrlQp = &commDev->ctrlQp;

  if (!ctrlQp->qp || !ctrlQp->ah) {
    WARN("Control QP not initialized: qp=%p, ah=%p", ctrlQp->qp, ctrlQp->ah);
    return flagcxInternalError;
  }

  static __thread int ack_send_count_debug = 0;
  if ((ack_send_count_debug++ % 100) == 0) {
    TRACE(FLAGCX_NET,
          "Sending ACK #%d: ack_seq=%u, sack_bitmap=0x%lx, remote_qpn=%u",
          ack_send_count_debug, ack_msg->ackSeq,
          (unsigned long)ack_msg->sackBitmap, ctrlQp->remoteQpn);
  }

  int total_polled = 0;
  for (int poll_iter = 0; poll_iter < 8; poll_iter++) {
    struct ibv_wc wcs[32];
    int n_cqe = 0;
    flagcxWrapIbvPollCq(ctrlQp->cq, 32, wcs, &n_cqe);
    total_polled += n_cqe;
    if (n_cqe == 0)
      break;
  }

  if (total_polled > 0) {
    TRACE(FLAGCX_NET, "Polled %d ACK completions before sending", total_polled);
  }

  struct flagcxIbAckMsg *ack_buf = (struct flagcxIbAckMsg *)commDev->ackBuffer;
  if (!ack_buf)
    return flagcxInternalError;

  memcpy(ack_buf, ack_msg, sizeof(struct flagcxIbAckMsg));

  struct ibv_sge sge;
  sge.addr = (uint64_t)ack_buf;
  sge.length = sizeof(struct flagcxIbAckMsg);
  sge.lkey = commDev->ackMr->lkey;

  struct ibv_send_wr wr;
  memset(&wr, 0, sizeof(wr));
  wr.wr_id = 0;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.opcode = IBV_WR_SEND;

  wr.send_flags = IBV_SEND_INLINE | IBV_SEND_SIGNALED;

  wr.wr.ud.ah = ctrlQp->ah;
  wr.wr.ud.remote_qpn = ctrlQp->remoteQpn;
  wr.wr.ud.remote_qkey = ctrlQp->remoteQkey;

  struct ibv_send_wr *bad_wr;
  flagcxResult_t result = flagcxWrapIbvPostSend(ctrlQp->qp, &wr, &bad_wr);

  if (result != flagcxSuccess) {
    int total_polled = 0;
    for (int i = 0; i < 4; i++) {
      struct ibv_wc wcs[32];
      int n = 0;
      flagcxWrapIbvPollCq(ctrlQp->cq, 32, wcs, &n);
      total_polled += n;
      if (n == 0)
        break;
    }

    TRACE(FLAGCX_NET, "ACK send failed, polled %d completions, retrying...",
          total_polled);

    result = flagcxWrapIbvPostSend(ctrlQp->qp, &wr, &bad_wr);
    if (result != flagcxSuccess) {
      WARN("Failed to send ACK after retry (polled %d completions)",
           total_polled);
      return result;
    }
  }

  TRACE(FLAGCX_NET, "Sent ACK via UD: ack_seq=%u, sack_count=%u",
        ack_msg->ackSeq, ack_msg->sackBitmapCount);

  return flagcxSuccess;
}

flagcxResult_t flagcxIbRetransRecvAckViaUd(struct flagcxIbSendComm *comm,
                                           int devIndex) {

  if (!comm || devIndex >= comm->base.ndevs) {
    return flagcxInternalError;
  }

  struct flagcxIbSendCommDev *commDev = &comm->devs[devIndex];
  struct flagcxIbCtrlQp *ctrlQp = &commDev->ctrlQp;

  if (!ctrlQp->qp || !ctrlQp->cq)
    return flagcxInternalError;

  struct ibv_wc wc;
  int n_cqe = 0;
  flagcxResult_t result = flagcxWrapIbvPollCq(ctrlQp->cq, 1, &wc, &n_cqe);

  if (result != flagcxSuccess || n_cqe == 0) {
    return flagcxSuccess;
  }

  static __thread int ack_recv_count_debug = 0;
  if ((ack_recv_count_debug++ % 100) == 0) {
    TRACE(FLAGCX_NET, "Polled ACK completion #%d: n_cqe=%d, status=%d",
          ack_recv_count_debug, n_cqe, wc.status);
  }

  if (wc.status != IBV_WC_SUCCESS) {
    WARN("ACK completion with error: status=%d", wc.status);
    return flagcxRemoteError;
  }

  int buf_idx = (int)wc.wr_id;
  size_t buf_entry_size = sizeof(struct flagcxIbAckMsg) + FLAGCX_IB_ACK_BUF_PADDING;
  char *buf_base = (char *)commDev->ackBuffer + buf_idx * buf_entry_size;
  struct flagcxIbAckMsg *ack_msg = (struct flagcxIbAckMsg *)(buf_base + FLAGCX_IB_ACK_BUF_PADDING);

  TRACE(
      FLAGCX_NET,
      "Received ACK: ack_seq=%u, sack_count=%u, sack_bitmap=0x%lx, buf_idx=%d",
      ack_msg->ackSeq, ack_msg->sackBitmapCount,
      (unsigned long)ack_msg->sackBitmap, buf_idx);

  FLAGCXCHECK(flagcxIbRetransProcessAck(&comm->retrans, ack_msg));

  struct ibv_sge sge;
  sge.addr = (uint64_t)buf_base;
  sge.length = buf_entry_size;
  sge.lkey = commDev->ackMr->lkey;

  struct ibv_recv_wr recv_wr;
  memset(&recv_wr, 0, sizeof(recv_wr));
  recv_wr.wr_id = buf_idx;
  recv_wr.next = NULL;
  recv_wr.sg_list = &sge;
  recv_wr.num_sge = 1;

  struct ibv_recv_wr *bad_wr;
  FLAGCXCHECK(flagcxWrapIbvPostRecv(ctrlQp->qp, &recv_wr, &bad_wr));

  TRACE(FLAGCX_NET, "Re-posted recv WR for ACK buffer %d", buf_idx);

  return flagcxSuccess;
}

flagcxResult_t flagcxIbCreateSrq(struct ibv_context *context, struct ibv_pd *pd,
                                 struct flagcxIbSrqMgr *srqMgr) {

  if (!context || !pd || !srqMgr)
    return flagcxInternalError;

  memset(srqMgr, 0, sizeof(struct flagcxIbSrqMgr));

  FLAGCXCHECK(flagcxWrapIbvCreateCq(&srqMgr->cq, context,
                                    FLAGCX_IB_SRQ_SIZE * 2, NULL, NULL, 0));

  struct ibv_srq_init_attr srq_attr;
  memset(&srq_attr, 0, sizeof(srq_attr));
  srq_attr.attr.max_wr = FLAGCX_IB_SRQ_SIZE;
  srq_attr.attr.max_sge = 1;

  struct ibv_srq *srq;
  flagcxResult_t result = flagcxWrapIbvCreateSrq(&srq, pd, &srq_attr);
  if (result != flagcxSuccess) {
    WARN("Failed to create SRQ (likely SRQ not supported or symbols not "
         "loaded)");
    flagcxWrapIbvDestroyCq(srqMgr->cq);
    return flagcxInternalError;
  }
  srqMgr->srq = (void *)srq;

  TRACE(FLAGCX_NET, "SRQ created successfully: %p", srq);

  size_t buf_size =
      FLAGCX_IB_RETRANS_MAX_CHUNK_SIZE + sizeof(struct flagcxIbRetransHdr);
  for (int i = 0; i < FLAGCX_IB_SRQ_SIZE; i++) {
    srqMgr->bufs[i].buffer = malloc(buf_size);
    if (!srqMgr->bufs[i].buffer) {
      WARN("Failed to allocate SRQ buffer %d", i);
      for (int j = 0; j < i; j++) {
        if (srqMgr->bufs[j].mr)
          flagcxWrapIbvDeregMr(srqMgr->bufs[j].mr);
        free(srqMgr->bufs[j].buffer);
      }
      flagcxWrapIbvDestroySrq(srq);
      flagcxWrapIbvDestroyCq(srqMgr->cq);
      return flagcxInternalError;
    }

    srqMgr->bufs[i].size = buf_size;
    srqMgr->bufs[i].inUse = 0;

    FLAGCXCHECK(flagcxWrapIbvRegMr(&srqMgr->bufs[i].mr, pd,
                                   srqMgr->bufs[i].buffer, buf_size,
                                   IBV_ACCESS_LOCAL_WRITE));

    TRACE(FLAGCX_NET, "SRQ buffer[%d]: addr=%p, size=%lu, lkey=0x%x", i,
          srqMgr->bufs[i].buffer, buf_size, srqMgr->bufs[i].mr->lkey);
  }

  srqMgr->bufCount = FLAGCX_IB_SRQ_SIZE;

  // Initialize buffer pool management (similar to UCCL)
  // All buffers start as free
  for (int i = 0; i < FLAGCX_IB_SRQ_SIZE; i++) {
    srqMgr->freeBufIndices[i] = i;
  }
  srqMgr->freeBufCount = FLAGCX_IB_SRQ_SIZE;
  srqMgr->postSrqCount = 0;

  TRACE(FLAGCX_NET,
        "Created SRQ: max_wr=%d, buf_size=%lu, srq=%p, free_buffers=%d",
        FLAGCX_IB_SRQ_SIZE, (unsigned long)buf_size, srqMgr->srq,
        srqMgr->freeBufCount);

  return flagcxSuccess;
}

flagcxResult_t flagcxIbDestroySrq(struct flagcxIbSrqMgr *srqMgr) {
  if (!srqMgr)
    return flagcxSuccess;

  for (int i = 0; i < srqMgr->bufCount; i++) {
    if (srqMgr->bufs[i].mr) {
      flagcxWrapIbvDeregMr(srqMgr->bufs[i].mr);
    }
    if (srqMgr->bufs[i].buffer) {
      free(srqMgr->bufs[i].buffer);
    }
  }

  if (srqMgr->srq) {
    flagcxWrapIbvDestroySrq((struct ibv_srq *)srqMgr->srq);
    srqMgr->srq = NULL;
  }

  if (srqMgr->cq) {
    flagcxWrapIbvDestroyCq(srqMgr->cq);
    srqMgr->cq = NULL;
  }

  return flagcxSuccess;
}

// Post recv WR to SRQ (similar to UCCL's check_srq)
flagcxResult_t flagcxIbSrqPostRecv(struct flagcxIbSrqMgr *srqMgr, int count) {

  if (!srqMgr || !srqMgr->srq)
    return flagcxInternalError;

  // Check if we have enough free buffers and need to post
  if (srqMgr->freeBufCount == 0 || srqMgr->postSrqCount == 0) {
    return flagcxSuccess;
  }

  struct ibv_srq *srq = (struct ibv_srq *)srqMgr->srq;

  // Limit post batch to available resources
  int post_batch = count;
  if (post_batch > srqMgr->postSrqCount)
    post_batch = srqMgr->postSrqCount;
  if (post_batch > srqMgr->freeBufCount)
    post_batch = srqMgr->freeBufCount;

  if (post_batch == 0)
    return flagcxSuccess;

  // Prepare recv WRs (use static array to avoid malloc)
  static __thread struct ibv_recv_wr recv_wrs[64];
  static __thread struct ibv_sge recv_sges[64];

  if (post_batch > 64)
    post_batch = 64;

  for (int i = 0; i < post_batch; i++) {
    // Pop a free buffer from the stack (similar to UCCL's pop_retr_chunk)
    srqMgr->freeBufCount--;
    int buf_idx = srqMgr->freeBufIndices[srqMgr->freeBufCount];

    // Setup SGE
    recv_sges[i].addr = (uint64_t)srqMgr->bufs[buf_idx].buffer;
    recv_sges[i].length = srqMgr->bufs[buf_idx].size;
    recv_sges[i].lkey = srqMgr->bufs[buf_idx].mr->lkey;

    // Setup recv WR
    // wr_id stores buffer index for easy lookup
    memset(&recv_wrs[i], 0, sizeof(recv_wrs[i]));
    recv_wrs[i].wr_id = buf_idx;
    recv_wrs[i].sg_list = &recv_sges[i];
    recv_wrs[i].num_sge = 1;
    recv_wrs[i].next = (i == post_batch - 1) ? NULL : &recv_wrs[i + 1];

    // Mark buffer as in use
    srqMgr->bufs[buf_idx].inUse = 1;

    TRACE(FLAGCX_NET, "Posting SRQ recv[%d]: buf_idx=%d, buffer=%p, wr_id=%d",
          i, buf_idx, srqMgr->bufs[buf_idx].buffer, buf_idx);
  }

  // Batch post (linked list)
  struct ibv_recv_wr *bad_wr;
  flagcxResult_t ret = flagcxWrapIbvPostSrqRecv(srq, &recv_wrs[0], &bad_wr);
  if (ret != flagcxSuccess) {
    WARN("Failed to batch post %d recv WRs to SRQ", post_batch);
    return flagcxRemoteError;
  }

  // Decrease post counter
  srqMgr->postSrqCount -= post_batch;

  TRACE(FLAGCX_NET,
        "Posted %d recv WRs to SRQ (free_buf_count=%d, post_srq_count=%d)",
        post_batch, srqMgr->freeBufCount, srqMgr->postSrqCount);

  return flagcxSuccess;
}
