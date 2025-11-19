#include "flagcx.h"
#include "tools.h"

#include "bootstrap.h"
#include "comm.h"
#include "device.h"
#include "flagcx/adaptor/include/adaptor.h"
#include "flagcx/adaptor/include/ib_common.h"
#include "flagcx_net.h"
#include "global_comm.h"
#include "net.h"
#include "alloc.h"
#include "check.h"

#include <algorithm>
#include <cassert>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sched.h>

namespace {

void fatal(flagcxResult_t res, const char *msg, int rank) {
  if (res != flagcxSuccess) {
    fprintf(stderr, "[rank %d] %s (err=%d)\n", rank, msg, int(res));
    MPI_Abort(MPI_COMM_WORLD, res);
  }
}
} // namespace

int main(int argc, char *argv[]) {
  parser args(argc, argv);
  size_t min_bytes = args.getMinBytes();
  size_t max_bytes = args.getMaxBytes();
  int step_factor = args.getStepFactor();
  int num_warmup_iters = 1;
  int num_iters = 0;
  int print_buffer = args.isPrintBuffer();
  uint64_t split_mask = args.getSplitMask();

  flagcxHandlerGroup_t handler;
  flagcxHandleInit(&handler);
  flagcxUniqueId_t &uniqueId = handler->uniqueId;
  flagcxComm_t &comm = handler->comm;
  flagcxDeviceHandle_t &devHandle = handler->devHandle;

  int color = 0;
  int worldSize = 1, worldRank = 0;
  int totalProcs = 1, proc = 0;
  MPI_Comm splitComm;
  initMpiEnv(argc, argv, worldRank, worldSize, proc, totalProcs, color,
             splitComm, split_mask);

  printf("[test_put] rank %d/%d proc %d/%d: entered main\n", worldRank,
         worldSize, proc, totalProcs);
  fflush(stdout);

  int nGpu;
  devHandle->getDeviceCount(&nGpu);
  devHandle->setDevice(worldRank % nGpu);

  if (proc == 0)
    flagcxGetUniqueId(&uniqueId);
  MPI_Bcast((void *)uniqueId, sizeof(flagcxUniqueId), MPI_BYTE, 0, splitComm);
  MPI_Barrier(MPI_COMM_WORLD);

  flagcxCommInitRank(&comm, totalProcs, uniqueId, proc);
  printf("[test_put] proc %d: comm initialised\n", proc);
  fflush(stdout);

  struct flagcxComm *innerComm = comm;
  struct flagcxHeteroComm *hetero = innerComm->hetero_comm;
  if (hetero == nullptr) {
    int isHomo = 0;
    flagcxIsHomoComm(comm, &isHomo);
    if (proc == 0) {
      printf("Skipping put benchmark: hetero communicator not initialised "
             "(isHomo=%d).\n",
             isHomo);
    }
    flagcxCommDestroy(comm);
    flagcxHandleFree(handler);
    MPI_Finalize();
    return 0;
  }

  struct flagcxNetAdaptor *netAdaptor = hetero->netAdaptor;
  if (netAdaptor == nullptr || netAdaptor->put == nullptr) {
    printf("[test_put] proc %d: net adaptor missing put (netAdaptor=%p)\n",
           proc, netAdaptor);
    fflush(stdout);
    if (proc == 0)
      fprintf(stderr, "Current network adaptor does not support put\n");
    MPI_Finalize();
    return 0;
  }

  if (totalProcs < 2) {
    if (proc == 0)
      printf("test_put requires at least 2 MPI processes\n");
    MPI_Finalize();
    return 0;
  }

  size_t signalBytes = sizeof(uint64_t);
  size_t window_bytes = max_bytes * 2 + signalBytes;

  void *window = nullptr;
  if (posix_memalign(&window, 64, window_bytes) != 0 || window == nullptr) {
    fprintf(stderr,
            "[rank %d] posix_memalign failed for host window (size=%zu)\n",
            proc, window_bytes);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  std::memset(window, 0, window_bytes);

  const int senderRank = 0;
  const int receiverRank = 1;
  if (totalProcs != 2) {
    if (proc == 0)
      printf("test_put requires exactly 2 ranks (sender=0, receiver=1).\n");
    MPI_Finalize();
    return 0;
  }

  bool isSender = (proc == senderRank);
  bool isReceiver = (proc == receiverRank);

  int sendRank = (proc + 1) % totalProcs;
  int recvRank = (proc - 1 + totalProcs) % totalProcs;

  flagcxNetHandle_t listenHandle = {};
  void *listenComm = nullptr;
  flagcxResult_t res =
      netAdaptor->listen(hetero->netDev, &listenHandle, &listenComm);
  printf("[test_put] proc %d: listen done res=%d listenComm=%p\n", proc,
         int(res), listenComm);
  fflush(stdout);
  fatal(res, "listen failed", proc);

  flagcxNetHandle_t peerHandle = {};
  printf("[test_put] proc %d: send listenHandle to recvRank %d, waiting for "
         "peerHandle from sendRank %d\n",
         proc, recvRank, sendRank);
  fflush(stdout);
  MPI_Sendrecv(&listenHandle, sizeof(listenHandle), MPI_BYTE, recvRank, 100,
               &peerHandle, sizeof(peerHandle), MPI_BYTE, sendRank, 100,
               MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  printf("[test_put] proc %d: got peerHandle from sendRank %d\n", proc,
         sendRank);
  fflush(stdout);

  void *sendComm = nullptr;
  void *recvComm = nullptr;
  while (sendComm == nullptr || recvComm == nullptr) {
    if (sendComm == nullptr) {
      res = netAdaptor->connect(hetero->netDev, &peerHandle, &sendComm);
      fatal(res, "connect failed", proc);
      if (res != flagcxSuccess || sendComm == nullptr) {
        printf("[test_put] proc %d: connect retry res=%d sendComm=%p\n", proc,
               int(res), sendComm);
      } else {
        printf("[test_put] proc %d: connect success res=%d sendComm=%p\n", proc,
               int(res), sendComm);
      }
      fflush(stdout);
    }

    if (recvComm == nullptr) {
      res = netAdaptor->accept(listenComm, &recvComm);
      fatal(res, "accept failed", proc);
      if (res != flagcxSuccess || recvComm == nullptr) {
        printf("[test_put] proc %d: accept retry res=%d recvComm=%p\n", proc,
               int(res), recvComm);
      } else {
        printf("[test_put] proc %d: accept success res=%d recvComm=%p\n", proc,
               int(res), recvComm);
      }
      fflush(stdout);
    }

    if (sendComm == nullptr || recvComm == nullptr) {
      sched_yield();
    }
  }

  res = netAdaptor->closeListen(listenComm);
  fatal(res, "closeListen failed", proc);

  void *mrHandle = nullptr;
  void *globalHandles = nullptr;
  if (netAdaptor->put == nullptr || netAdaptor->putSignal == nullptr ||
      netAdaptor->waitValue == nullptr || netAdaptor->test == nullptr ||
      netAdaptor->deregMr == nullptr) {
    fprintf(stderr,
            "[rank %d] Net adaptor does not support one-sided extensions\n",
            proc);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  // Choose regComm based on role: sender uses sendComm, receiver uses recvComm
  // Both need to register to get their rkey for RDMA operations
  void *regComm = isSender ? sendComm : recvComm;
  if (netAdaptor == &flagcxNetIb) {
    if (isSender && sendComm != nullptr) {
      auto *ibSendComm = reinterpret_cast<struct flagcxIbSendComm *>(sendComm);
      regComm = static_cast<void *>(&ibSendComm->base);
    } else if (isReceiver && recvComm != nullptr) {
      auto *ibRecvComm = reinterpret_cast<struct flagcxIbRecvComm *>(recvComm);
      regComm = static_cast<void *>(&ibRecvComm->base);
    }
  }

  // Register buffer logic (moved from flagcxIbRegBuffer)
  struct bootstrapState *state = innerComm->bootstrap;
  
  // Register with net adaptor to get MR handle for IB operations
  res = netAdaptor->regMr(regComm, window, window_bytes, FLAGCX_PTR_HOST,
                          &mrHandle);
  fatal(res, "netAdaptor->regMr failed", proc);
  
  // Convert mrHandle (flagcxIbMrHandle*) to ibv_mr*
  struct flagcxIbMrHandle *localMrHandle =
      (struct flagcxIbMrHandle *)mrHandle;
  struct ibv_mr *mr = localMrHandle->mrs[0];

  int nranks = state->nranks;

  struct flagcxIbGlobalHandleInfo *info = nullptr;
  res = flagcxCalloc(&info, 1);
  fatal(res, "flagcxCalloc failed for info", proc);
  res = flagcxCalloc(&info->base_vas, nranks);
  fatal(res, "flagcxCalloc failed for base_vas", proc);
  res = flagcxCalloc(&info->rkeys, nranks);
  fatal(res, "flagcxCalloc failed for rkeys", proc);
  res = flagcxCalloc(&info->lkeys, nranks);
  fatal(res, "flagcxCalloc failed for lkeys", proc);

  info->base_vas[state->rank] = (uintptr_t)mr->addr;
  info->rkeys[state->rank] = mr->rkey;
  info->lkeys[state->rank] = mr->lkey;
  
  printf("[test_put] proc %d: BEFORE allgather - rank %d: base_va=0x%lx rkey=0x%x lkey=0x%x\n",
         proc, state->rank, info->base_vas[state->rank], info->rkeys[state->rank], info->lkeys[state->rank]);
  fflush(stdout);
  
  res = bootstrapAllGather(innerComm->bootstrap, (void *)info->base_vas,
                            sizeof(uintptr_t));
  fatal(res, "bootstrapAllGather failed for base_vas", proc);
  res = bootstrapAllGather(innerComm->bootstrap, (void *)info->rkeys,
                           sizeof(uint32_t));
  fatal(res, "bootstrapAllGather failed for rkeys", proc);
  res = bootstrapAllGather(innerComm->bootstrap, (void *)info->lkeys,
                           sizeof(uint32_t));
  fatal(res, "bootstrapAllGather failed for lkeys", proc);
  
  printf("[test_put] proc %d: AFTER allgather - all ranks:\n", proc);
  for (int i = 0; i < nranks; ++i) {
    printf("  rank %d: base_va=0x%lx rkey=0x%x lkey=0x%x\n",
           i, info->base_vas[i], info->rkeys[i], info->lkeys[i]);
  }
  fflush(stdout);

  globalHandles = (void *)info;

  printf("[test_put] proc %d: regBuffer done res=%d mrHandle=%p mr=%p "
         "globalHandles=%p window=%p bytes=%zu\n",
         proc, int(res), mrHandle, mr, globalHandles, window, window_bytes);
  fflush(stdout);

  timer tim;
  size_t sendOffset = 0;
  size_t recvOffset = 0;
  size_t signalOffset = max_bytes * 2;

  for (size_t size = min_bytes; size <= max_bytes; size *= step_factor) {
    if (size == 0)
      break;

    uint64_t expectedSignal = 0;

    for (int i = 0; i < num_warmup_iters; ++i) {
      if (isSender) {
        uint8_t value = static_cast<uint8_t>((senderRank + i) & 0xff);
        std::memset((char *)window + sendOffset, value, size);

        void *putReq = nullptr;
        res =
            netAdaptor->put(sendComm, sendOffset, recvOffset, size, senderRank,
                            receiverRank, (void **)globalHandles, &putReq);
        fatal(res, "netAdaptor->put warmup failed", proc);

        void *sigReq = nullptr;
        res = netAdaptor->putSignal(sendComm, signalOffset, senderRank,
                                     senderRank, receiverRank,
                                     (void **)globalHandles, &sigReq);
        fatal(res, "netAdaptor->putSignal warmup failed", proc);

        expectedSignal += 1;
      } else if (isReceiver) {
        expectedSignal += 1;
        res = netAdaptor->waitValue((void **)globalHandles, receiverRank,
                                    signalOffset, expectedSignal);
        fatal(res, "netAdaptor->waitValue warmup failed", proc);
      }
    }

    MPI_Barrier(MPI_COMM_WORLD);
    if (proc == senderRank) {
      printf("[test_put] warmup done size=%zu\n", size);
      fflush(stdout);
    }
    tim.reset();

    for (int i = 0; i < num_iters; ++i) {
      if (isSender) {
        uint8_t value = static_cast<uint8_t>((senderRank + i) & 0xff);
        std::memset((char *)window + sendOffset, value, size);

        void *putReq = nullptr;
        res =
            netAdaptor->put(sendComm, sendOffset, recvOffset, size, senderRank,
                            receiverRank, (void **)globalHandles, &putReq);
        fatal(res, "netAdaptor->put failed", proc);

        void *sigReq = nullptr;
        res = netAdaptor->putSignal(sendComm, signalOffset, senderRank,
                                     senderRank, receiverRank,
                                     (void **)globalHandles, &sigReq);
        fatal(res, "netAdaptor->putSignal failed", proc);

        expectedSignal += 1;
      } else if (isReceiver) {
        expectedSignal += 1;
        res = netAdaptor->waitValue((void **)globalHandles, receiverRank,
                                    signalOffset, expectedSignal);
        fatal(res, "netAdaptor->waitValue failed", proc);

        uint8_t expected = static_cast<uint8_t>((senderRank + i) & 0xff);
        uint8_t *ptr = (uint8_t *)window + recvOffset;
        for (size_t j = 0; j < size; ++j) {
          if (ptr[j] != expected) {
            fprintf(
                stderr,
                "[rank %d] Verification failed at byte %zu (expected %u got %u)"
                " for iteration %d size %zu\n",
                proc, j, expected, ptr[j], i, size);
            MPI_Abort(MPI_COMM_WORLD, 2);
          }
        }
        if ((proc == 0 || proc == totalProcs - 1) && print_buffer) {
          printf("[rank %d] recv data (first 16 bytes): ", proc);
          for (size_t j = 0; j < std::min<size_t>(size, 16); ++j) {
            printf("%02x ", ptr[j]);
          }
          printf("\n");
        }
      }
    }
    if (proc == senderRank) {
      printf("[test_put] completed %d iterations size=%zu\n", num_iters, size);
      fflush(stdout);
    }
    double elapsed_time = tim.elapsed() / num_iters;
    MPI_Allreduce(MPI_IN_PLACE, &elapsed_time, 1, MPI_DOUBLE, MPI_SUM,
                  MPI_COMM_WORLD);
    elapsed_time /= worldSize;

    double bandwidth = (double)size / 1.0e9 / elapsed_time;
    if (proc == 0 && color == 0) {
      printf("Size: %zu bytes; Avg time: %lf sec; Bandwidth: %lf GB/s\n", size,
             elapsed_time, bandwidth);
    }

    MPI_Barrier(MPI_COMM_WORLD);
  }

  // Deregister using the same comm that was used for registration
  res = netAdaptor->deregMr(regComm, mrHandle);
  fatal(res, "netAdaptor->deregMr failed", proc);
  
  // Deregister buffer logic (moved from flagcxIbDeregBuffer)
  if (globalHandles != nullptr) {
    struct flagcxIbGlobalHandleInfo *info =
        (struct flagcxIbGlobalHandleInfo *)globalHandles;
    free(info->base_vas);
    free(info->rkeys);
    free(info->lkeys);
    free(info);
    globalHandles = nullptr;
  }

  res = netAdaptor->closeSend(sendComm);
  fatal(res, "closeSend failed", proc);
  res = netAdaptor->closeRecv(recvComm);
  fatal(res, "closeRecv failed", proc);

  free(window);

  fatal(flagcxCommDestroy(comm), "flagcxCommDestroy failed", proc);
  fatal(flagcxHandleFree(handler), "flagcxHandleFree failed", proc);

  MPI_Finalize();
  return 0;
}
