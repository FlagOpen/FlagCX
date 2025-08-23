#ifdef USE_UCX

/*************************************************************************
 * SPDX-FileCopyrightText: NVIDIA CORPORATION & AFFILIATES
 * Copyright (c) 2024-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#include "ucx_uct_lib.h"

typedef enum {
  FLAGCX_UCT_REQ_IRECV  = -1,
  FLAGCX_UCT_REQ_IFLUSH = -2
} flagcx_uct_request_type_t;

struct flagcx_uct_rdesc;

/* On-the-wire descriptor of a posted receive request entry */
typedef struct {
  int        tag;
  int        size;
  void       *data;
  int        matched;
  uct_rkey_t rkey;
} flagcx_uct_chunk_t;

/* On-the-wire descriptor of a receive request containing many chunks */
typedef struct {
  uint64_t              id;
  uint16_t              count;
  uint32_t              size;
  struct flagcx_uct_rdesc *peer_rdesc; /* Acts as a cookie along with id */
  flagcx_uct_chunk_t      chunk[FLAGCX_UCX_UCT_MAX_RECVS];
} flagcx_uct_rdesc_hdr_t;

/* On-the-wire descriptor for receive request completion */
typedef struct {
  uint64_t              id;
  struct flagcx_uct_rdesc *rdesc;
  int                   count; /* Number of sizes contained */
  int                   sizes[FLAGCX_UCX_UCT_MAX_RECVS];
} flagcx_uct_atp_t;

/*
 * flagcx local request handler to progress:
 * - size -1 for multi receive
 * - size -2 for flush
 * - size > 0 for send
 */
typedef struct {
  /* Pending GET (iflush) PUT (isend) or receiving one ATP (irecv) */
  uct_completion_t      completion;
  int                   size;
  struct flagcx_uct_rdesc *rdesc;
} flagcx_uct_req_t;

/* Pending receive descriptor either on the receive or sending side */
typedef struct flagcx_uct_rdesc {
  int                   flagcx_usage; /* flagcx requests not finished/started */
  int                   send_atp;   /* >1 pending isend, ==1 pending atp send */

  union {
    ucs_list_link_t       list;  /* comm's linked list */
    struct flagcx_uct_rdesc *next; /* inserted in free list */
  };

  struct flagcx_uct_wr_comm  *comm;
  flagcx_uct_rdesc_hdr_t     desc;
  flagcx_uct_req_t           reqs[FLAGCX_UCX_UCT_MAX_RECVS];    /* flagcx requests */
  int                      sizes[FLAGCX_UCX_UCT_MAX_RECVS];   /* ATP received sizes */
  flagcx_uct_chunk_t         storage[FLAGCX_UCX_UCT_MAX_RECVS]; /* Don't use directly */
} flagcx_uct_rdesc_t;

typedef struct flagcx_uct_wr_comm {
  flagcx_uct_comm_t      base;

  int                  rdesc_alloc; /* Track allocated rdescs */
  flagcx_uct_rdesc_t     *free_rdesc; /* Available rdesc for reuse */
  uint64_t             rdesc_id;    /* Next sequence number to use */

  /* Received RTRs: used by Sender communicator in ->isend() */
  ucs_list_link_t      rdesc_list;

} flagcx_uct_wr_comm_t;

static inline flagcx_uct_wr_comm_t *
flagcx_uct_wr_comm_get(flagcx_uct_comm_t *base_comm) {
  return ucs_container_of(base_comm, flagcx_uct_wr_comm_t, base);
}

static flagcx_uct_rdesc_t *flagcx_uct_comm_rdesc_get(flagcx_uct_wr_comm_t *comm) {
  flagcx_uct_rdesc_t *rdesc = comm->free_rdesc;

  if (rdesc == NULL) {
    rdesc = (flagcx_uct_rdesc_t*)calloc(1, sizeof(*rdesc));
  } else {
    comm->free_rdesc = rdesc->next;
  }

  rdesc->next = NULL;
  rdesc->comm = comm;
  comm->rdesc_alloc++;
  return rdesc;
}

static size_t flagcx_uct_rdesc_size(int n) {
  return n * sizeof(flagcx_uct_chunk_t) + sizeof(flagcx_uct_rdesc_hdr_t);
}

/* Prepare a receive descriptor from irecv()/iflush() side */
static void flagcx_uct_rdesc_set(flagcx_uct_rdesc_t *rdesc, uint64_t id, int n,
                               void **data, size_t *sizes, int *tags,
                               flagcx_uct_memh_t **uct_memh) {
  flagcx_uct_rdesc_hdr_t *desc = &rdesc->desc;
  int i;

  /* Populate header */
  desc->id         = id;
  desc->count      = n;
  desc->size       = flagcx_uct_rdesc_size(n);
  desc->peer_rdesc = rdesc; /* cookie, will be returned in ATP */

  /* Ref count that prevents flagcx from releasing memory */
  rdesc->flagcx_usage = 1;
  rdesc->send_atp   = 0;

  /* Zero (iflush) or one or many receive request are contained */
  for (i = 0; i < n; i++) {
    desc->chunk[i].tag     = tags[i];
    desc->chunk[i].size    = sizes[i];
    desc->chunk[i].data    = data[i];
    desc->chunk[i].matched = 0;
    desc->chunk[i].rkey    = uct_memh[i]->bundle.rkey;
  }
}

static flagcx_uct_req_t *flagcx_uct_rdesc_get_req(flagcx_uct_rdesc_t *rdesc, int i,
                                              int size) {
  flagcx_uct_req_t *req;

  assert(i < FLAGCX_UCX_UCT_MAX_RECVS);

  req        = &rdesc->reqs[i];
  req->size  = size;
  req->rdesc = rdesc;

  req->completion.func   = flagcx_uct_empty_callback;
  req->completion.count  = 1;
  req->completion.status = UCS_OK;

  return &rdesc->reqs[i];
}

static void flagcx_uct_comm_rdesc_put(flagcx_uct_rdesc_t *rdesc) {
  flagcx_uct_wr_comm_t *comm = rdesc->comm;

  assert(comm != NULL);

  rdesc->desc.id   = -1;
  rdesc->comm      = NULL;
  rdesc->next      = comm->free_rdesc;
  comm->free_rdesc = rdesc;
  comm->rdesc_alloc--;
}

/* On receiver side, after ->irecv(), expect corresponding ATP */
static ucs_status_t flagcx_uct_atp_callback(void *arg, void *data, size_t length,
                                          unsigned flags) {
  flagcx_uct_atp_t *atp = (flagcx_uct_atp_t*)((uint8_t*)data + 8);

  assert(length == (sizeof(*atp) + 8));
  assert(*(flagcx_uct_comm_t**)data == &atp->rdesc->comm->base);
  assert(atp->id == atp->rdesc->desc.id);
  assert(atp->count == atp->rdesc->desc.count);
  assert(atp->rdesc->reqs[0].completion.count == 1);

  atp->rdesc->reqs[0].completion.count--;
  memcpy(atp->rdesc->sizes, atp->sizes, atp->count * sizeof(*atp->sizes));
  return UCS_OK;
}

/* On sender side, asynchronously receive rdesc/RTR, later used by ->isend() */
static ucs_status_t flagcx_uct_rtr_callback(void *arg, void *data, size_t length,
                                          unsigned flags) {
  flagcx_uct_comm_t *base_comm = *(flagcx_uct_comm_t **)data;
  flagcx_uct_wr_comm_t *comm   = flagcx_uct_wr_comm_get(base_comm);
  flagcx_uct_rdesc_hdr_t *desc = (flagcx_uct_rdesc_hdr_t*)((uint8_t*)data + 8);
  size_t size                = desc->size;
  flagcx_uct_rdesc_t *rdesc;

  rdesc = flagcx_uct_comm_rdesc_get(comm);
  if (rdesc == NULL) {
    WARN("Failed to get an rdesc in RTR callback");
    return UCS_ERR_NO_MEMORY; /* Cannot happend */
  }

  ucs_list_add_tail(&comm->rdesc_list, &rdesc->list);

  assert((size + 8) == length);
  assert(size == flagcx_uct_rdesc_size(desc->count));

  memcpy(&rdesc->desc, desc, size);
  rdesc->flagcx_usage = desc->count;
  rdesc->send_atp   = desc->count + 1;
  return UCS_OK;
}

static flagcxResult_t flagcx_uct_wr_iface_set(flagcx_uct_iface_t *uct_iface) {
  FLAGCXCHECK(flagcx_uct_iface_set_handler(uct_iface, FLAGCX_UCT_AM_RTR,
                                       flagcx_uct_rtr_callback));
  FLAGCXCHECK(flagcx_uct_iface_set_handler(uct_iface, FLAGCX_UCT_AM_ATP,
                                       flagcx_uct_atp_callback));
  return flagcxSuccess;
}

static flagcxResult_t flagcx_uct_wr_comm_alloc(flagcx_uct_comm_t **comm_p) {
  flagcx_uct_wr_comm_t *comm = (flagcx_uct_wr_comm_t*)calloc(1, sizeof(flagcx_uct_wr_comm_t));
  if (comm != NULL) {
    *comm_p = &comm->base;
    return flagcxSuccess;
  }

  return flagcxSystemError;
}

static flagcxResult_t flagcx_uct_wr_comm_init(flagcx_uct_comm_t *base_comm,
                                          flagcx_uct_context_t *context,
                                          flagcx_uct_worker_t *worker, int dev,
                                          const flagcx_uct_comm_t *remote_comm) {
  flagcx_uct_wr_comm_t *comm = flagcx_uct_wr_comm_get(base_comm);

  ucs_list_head_init(&comm->rdesc_list);
  return flagcx_uct_comm_init(&comm->base, context, worker, dev, remote_comm);
}

static flagcxResult_t flagcx_uct_wr_init(flagcxDebugLogger_t logFunction, void* profFunction) {
  context.ops.comm_alloc = flagcx_uct_wr_comm_alloc;
  context.ops.comm_init  = flagcx_uct_wr_comm_init;
  context.ops.iface_set  = flagcx_uct_wr_iface_set;
  context.am_short_size  = flagcx_uct_rdesc_size(FLAGCX_UCX_UCT_MAX_RECVS);
  context.rkey_size      = sizeof(((flagcx_uct_chunk_t*)0)->rkey);

  return flagcx_p2p_ib_init(&context.dev_count, &context.merge_dev_count, flagcxIbDevs, context.if_name,
                          &context.if_addr, NULL, logFunction);
}

static void flagcx_uct_send_atp(flagcx_uct_wr_comm_t *comm,
                              flagcx_uct_rdesc_t *rdesc) {
  ucs_status_t status;
  flagcx_uct_atp_t atp;
  int i;

  assert(rdesc->send_atp == 1);

  status = uct_ep_fence(comm->base.uct_ep->ep, 0);
  if (status != UCS_OK) {
    return;
  }

  atp.id    = rdesc->desc.id;
  atp.rdesc = rdesc->desc.peer_rdesc;
  atp.count = rdesc->desc.count;

  for (i = 0; i < rdesc->desc.count; i++) {
    atp.sizes[i] = rdesc->reqs[i].size;
  }

  status = uct_ep_am_short(comm->base.uct_ep->ep, FLAGCX_UCT_AM_ATP,
                           (uint64_t)comm->base.remote.comm, &atp, sizeof(atp));
  if (status == UCS_OK) {
    rdesc->send_atp = 0;
  }
}

static flagcxResult_t flagcx_uct_send(flagcx_uct_wr_comm_t *comm, void *data,
                                  int size, flagcx_uct_memh_t *uct_memh,
                                  flagcx_uct_rdesc_t *rdesc, int i,
                                  void **request) {
  ucs_status_t status;
  uct_iov_t iov;
  flagcx_uct_req_t *req;

  *request = NULL;

  iov.buffer = data;
  iov.length = size;
  iov.memh   = uct_memh->memh;
  iov.stride = iov.length;
  iov.count  = 1;

  assert(size <= rdesc->desc.chunk[i].size);

  req = flagcx_uct_rdesc_get_req(rdesc, i, size); 

  status = uct_ep_put_zcopy(comm->base.uct_ep->ep, &iov, 1,
                            (uint64_t)rdesc->desc.chunk[i].data,
                            rdesc->desc.chunk[i].rkey, &req->completion);

  if (status == UCS_OK) {
    req->completion.count--;
  } else if (status != UCS_INPROGRESS) {
    return flagcxSuccess;
  }

  rdesc->desc.chunk[i].matched = 1;
  --rdesc->send_atp;

  if (rdesc->send_atp == 1) {
    ucs_list_del(&rdesc->list); 
    flagcx_uct_send_atp(comm, rdesc);
  }

  *request = req;
  return flagcxSuccess;
}

static flagcxResult_t flagcx_uct_wr_isend(void *send_comm, void *data, size_t size,
                                      int tag, void *mhandle,  void* phandle, void **request) {
  flagcx_uct_wr_comm_t *comm = flagcx_uct_wr_comm_get((flagcx_uct_comm_t*)send_comm);
  flagcx_uct_rdesc_t *rdesc;
  int i;

  *request = NULL;

  ucs_list_for_each(rdesc, &comm->rdesc_list, list) {
    for (i = 0; i < rdesc->desc.count; i++) {
      if (rdesc->desc.chunk[i].matched || (rdesc->desc.chunk[i].tag != tag)) {
        continue;
      }

      return flagcx_uct_send(comm, data, size, (flagcx_uct_memh_t*)mhandle, rdesc, i, request);
    }
  }

  /* Progress here to make sure we receive non-solicited RTRs */
  uct_worker_progress(comm->base.uct_worker->worker);
  return flagcxSuccess;
}

static flagcxResult_t flagcx_uct_wr_irecv(void *recv_comm, int n, void **data,
                                      size_t *sizes, int *tags, void **mhandles,
                                      void** phandles, void **request) {
  flagcx_uct_wr_comm_t *comm   = flagcx_uct_wr_comm_get((flagcx_uct_comm_t*)recv_comm);
  flagcx_uct_memh_t **uct_memh = (flagcx_uct_memh_t**)mhandles;
  flagcx_uct_rdesc_t *rdesc;
  ucs_status_t status;

  assert(n <= FLAGCX_UCX_UCT_MAX_RECVS);

  rdesc = flagcx_uct_comm_rdesc_get(comm);
  if (rdesc == NULL) {
    return flagcxInternalError;
  }

  flagcx_uct_rdesc_set(rdesc, comm->rdesc_id++, n, data, sizes, tags, uct_memh);

  status = uct_ep_am_short(comm->base.uct_ep->ep, FLAGCX_UCT_AM_RTR,
                           (uint64_t)comm->base.remote.comm, &rdesc->desc,
                           flagcx_uct_rdesc_size(n));
  if (status != UCS_OK) {
    flagcx_uct_comm_rdesc_put(rdesc);
    *request = NULL;
  } else {
    *request = flagcx_uct_rdesc_get_req(rdesc, 0, FLAGCX_UCT_REQ_IRECV);
  }

  return flagcxSuccess;
}

static flagcxResult_t flagcx_uct_wr_iflush(void *recv_comm, int n, void **data,
                                       int *sizes, void **mhandle,
                                       void **request) {
  flagcx_uct_comm_t *base_comm = (flagcx_uct_comm_t*)recv_comm;
  int last                   = flagcx_uct_flush_index(base_comm, sizes, n);
  flagcx_uct_memh_t **uct_memh = (flagcx_uct_memh_t**)mhandle;
  flagcx_uct_rdesc_t *rdesc;
  flagcx_uct_req_t *req;
  flagcxResult_t result;

  if (last == -1) {
    return flagcxSuccess;
  }

  rdesc = flagcx_uct_comm_rdesc_get(flagcx_uct_wr_comm_get(base_comm));
  if (rdesc == NULL) {
    return flagcxInternalError;
  }

  flagcx_uct_rdesc_set(rdesc, ~0, 0, NULL, NULL, NULL, NULL);
  req = flagcx_uct_rdesc_get_req(rdesc, 0, FLAGCX_UCT_REQ_IFLUSH);
  *request = req;

  result = flagcx_uct_flush(base_comm, data[last], sizes[last], uct_memh[last],
                          &req->completion, request);
  if (*request == NULL) {
    flagcx_uct_comm_rdesc_put(rdesc);
  }

  return result;
}

static flagcxResult_t flagcx_uct_wr_test(void *request, int *done, int *sizes) {
  flagcx_uct_req_t *req      = (flagcx_uct_req_t*)request;
  flagcx_uct_rdesc_t *rdesc  = req->rdesc;
  flagcx_uct_wr_comm_t *comm = rdesc->comm;

  uct_worker_progress(comm->base.uct_worker->worker);

  *done = 0;

  if (rdesc->send_atp == 1) {
    flagcx_uct_send_atp(comm, rdesc);

    if (rdesc->send_atp && rdesc->flagcx_usage == 1) {
      return flagcxSuccess;
    }
  }

  if (req->completion.count > 0) {
    return flagcxSuccess;
  }

  *done = 1;

  if (req->size == FLAGCX_UCT_REQ_IRECV) {
    assert(&rdesc->reqs[0] == req);
    if (sizes != NULL) {
      memcpy(sizes, rdesc->sizes, rdesc->desc.count * sizeof(*sizes));
    }
  } else if (req->size == FLAGCX_UCT_REQ_IFLUSH) {
    assert(&rdesc->reqs[0] == req);
  } else {
    /* ->isend() request */
    assert(req->size > -1);
    if (sizes != NULL) {
      sizes[0] = req->size;
    }
  }

  if (--rdesc->flagcx_usage < 1) {
    assert(rdesc->send_atp == 0);
    assert(rdesc->flagcx_usage == 0);
    flagcx_uct_comm_rdesc_put(rdesc);
  }

  return flagcxSuccess;
}

static flagcxResult_t flagcx_uct_wr_close(void *close_comm) {
  flagcx_uct_wr_comm_t *comm = flagcx_uct_wr_comm_get((flagcx_uct_comm_t*)close_comm);
  flagcx_uct_rdesc_t *rdesc;

  flagcx_uct_comm_deinit((flagcx_uct_comm_t*)close_comm);

  while ((rdesc = comm->free_rdesc) != NULL) {
    comm->free_rdesc = rdesc->next;
    free(rdesc);
  }

  assert(ucs_list_is_empty(&comm->rdesc_list));
  assert(comm->rdesc_alloc == 0);
  free(comm);
  return flagcxSuccess;
}

static flagcxResult_t flagcx_uct_wr_init_v8(flagcxDebugLogger_t logFunction) {
  return flagcx_uct_wr_init(logFunction, NULL);
}

static flagcxResult_t flagcx_uct_wr_init_v7(flagcxDebugLogger_t logFunction) {
  return flagcx_uct_wr_init(logFunction, NULL);
}

static flagcxResult_t flagcx_uct_wr_init_v6(flagcxDebugLogger_t logFunction) {
  return flagcx_uct_wr_init(logFunction, NULL);
}

static flagcxResult_t flagcx_uct_wr_init_v5(flagcxDebugLogger_t logFunction) {
  return flagcx_uct_wr_init(logFunction, NULL);
}

static flagcxResult_t flagcx_uct_wr_isend_v8(void *send_comm, void *data, int size,
                                      int tag, void *mhandle, void **request) {
  return flagcx_uct_wr_isend(send_comm, data, (size_t)size, tag, mhandle, NULL, request);
}

static flagcxResult_t flagcx_uct_wr_irecv_v8(void *recv_comm, int n, void **data,
                                      int *sizes, int *tags, void **mhandles,
                                      void **request) {
  size_t sizes_sizet[FLAGCX_NET_IB_MAX_RECVS];
  for (int i=0; i<n; i++) sizes_sizet[i] = sizes[i];
  return flagcx_uct_wr_irecv(recv_comm, n, data, sizes_sizet, tags, mhandles, NULL, request);
}

flagcxNet_v8_t flagcxNetPlugin_v8 = FLAGCX_UCT_PLUGIN_V8("UCX-UCT", flagcx_uct_wr);
flagcxNet_v7_t flagcxNetPlugin_v7 = FLAGCX_UCT_PLUGIN_V7("UCX-UCT", flagcx_uct_wr);
flagcxNet_v6_t flagcxNetPlugin_v6 = FLAGCX_UCT_PLUGIN_V6("UCX-UCT", flagcx_uct_wr);
flagcxNet_v5_t flagcxNetPlugin_v5 = FLAGCX_UCT_PLUGIN_V5("UCX-UCT", flagcx_uct_wr);

#endif /* USE_UCX */
