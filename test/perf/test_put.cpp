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
  if (netAdaptor->regBuffer == nullptr || netAdaptor->put == nullptr ||
      netAdaptor->signalValue == nullptr || netAdaptor->waitValue == nullptr ||
      netAdaptor->test == nullptr || netAdaptor->deregMr == nullptr) {
    fprintf(stderr,
            "[rank %d] Net adaptor does not support one-sided extensions\n",
            proc);
    MPI_Abort(MPI_COMM_WORLD, 1);
  }
  void *regComm = sendComm;
  if (netAdaptor == &flagcxNetIb && sendComm != nullptr) {
    auto *ibSendComm = reinterpret_cast<struct flagcxIbSendComm *>(sendComm);
    regComm = static_cast<void *>(&ibSendComm->base);
  }

  res = netAdaptor->regBuffer(regComm, window, window_bytes, FLAGCX_PTR_HOST,
                              innerComm->bootstrap, &mrHandle, &globalHandles);
  printf("[test_put] proc %d: regBuffer done res=%d mrHandle=%p "
         "globalHandles=%p window=%p bytes=%zu\n",
         proc, int(res), mrHandle, globalHandles, window, window_bytes);
  fflush(stdout);
  fatal(res, "netAdaptor->regBuffer failed", proc);

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
        sleep(3);
        uint8_t value = static_cast<uint8_t>((senderRank + i) & 0xff);
        std::memset((char *)window + sendOffset, value, size);

        void *putReq = nullptr;
        res =
            netAdaptor->put(sendComm, sendOffset, recvOffset, size, senderRank,
                            receiverRank, (void **)globalHandles, &putReq);
        fatal(res, "netAdaptor->put warmup failed", proc);

        void *sigReq = nullptr;
        res = netAdaptor->signalValue(sendComm, signalOffset, senderRank,
                                      senderRank, receiverRank,
                                      (void **)globalHandles, &sigReq);
        fatal(res, "netAdaptor->signalValue warmup failed", proc);

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
        res = netAdaptor->signalValue(sendComm, signalOffset, senderRank,
                                      senderRank, receiverRank,
                                      (void **)globalHandles, &sigReq);
        fatal(res, "netAdaptor->signalValue failed", proc);

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

  res = netAdaptor->deregMr(sendComm, mrHandle);
  fatal(res, "netAdaptor->deregMr failed", proc);
  if (netAdaptor->deregBuffer) {
    fatal(netAdaptor->deregBuffer(sendComm, globalHandles),
          "netAdaptor->deregBuffer failed", proc);
  } else if (globalHandles != nullptr) {
    fprintf(stderr,
            "[rank %d] Warning: net adaptor does not expose deregBuffer; "
            "potential leak for global handles\n",
            proc);
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
