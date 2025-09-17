#include "flagcx.h"
#include "tools.h"
#include <cstring>
#include <iostream>

#define DATATYPE flagcxFloat

int main(int argc, char *argv[]) {
  parser args(argc, argv);
  size_t min_bytes = args.getMinBytes();
  size_t max_bytes = args.getMaxBytes();
  int step_factor = args.getStepFactor();
  int num_warmup_iters = args.getWarmupIters();
  int num_iters = args.getTestIters();
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

  int nGpu;
  devHandle->getDeviceCount(&nGpu);
  devHandle->setDevice(worldRank % nGpu);

  if (proc == 0)
    flagcxGetUniqueId(&uniqueId);
  MPI_Bcast((void *)uniqueId, sizeof(flagcxUniqueId), MPI_BYTE, 0, splitComm);
  MPI_Barrier(MPI_COMM_WORLD);

  flagcxCommInitRank(&comm, totalProcs, uniqueId, proc);

  flagcxStream_t stream;
  devHandle->streamCreate(&stream);

  void *sendbuff, *recvbuff, *hello;
  size_t *h_sendcounts, *h_recvcounts, *h_sdispls, *h_rdispls;
  size_t count, sdis, rdis;
  timer tim;

  devHandle->deviceMalloc((void **)&h_sendcounts, totalProcs * sizeof(size_t),
                          flagcxMemHost, NULL);
  devHandle->deviceMalloc((void **)&h_recvcounts, totalProcs * sizeof(size_t),
                          flagcxMemHost, NULL);
  devHandle->deviceMalloc((void **)&h_sdispls, totalProcs * sizeof(size_t),
                          flagcxMemHost, NULL);
  devHandle->deviceMalloc((void **)&h_rdispls, totalProcs * sizeof(size_t),
                          flagcxMemHost, NULL);
  devHandle->deviceMalloc(&sendbuff, max_bytes, flagcxMemDevice, NULL);
  devHandle->deviceMalloc(&recvbuff, max_bytes, flagcxMemDevice, NULL);
  devHandle->deviceMalloc(&hello, max_bytes, flagcxMemHost, NULL);
  devHandle->deviceMemset(hello, 0, max_bytes, flagcxMemHost, NULL);

  // Warm-up for large size
  sdis = 0;
  rdis = 0;
  count = (max_bytes / sizeof(float)) / totalProcs;
  for (int i = 0; i < totalProcs; i++) {
    if (proc % 2 == 0) {
      if (i % 2 == 0) {
        h_sendcounts[i] = 2 * count;
        h_recvcounts[i] = 2 * count;
        h_sdispls[i] = sdis;
        h_rdispls[i] = rdis;
        if (i == proc) {
          h_sendcounts[i] = 0;
          h_recvcounts[i] = 0;
        }
        sdis += 2 * count;
        rdis += 2 * count;
      } else {
        h_sendcounts[i] = 0;
        h_recvcounts[i] = 0;
        h_sdispls[i] = sdis;
        h_rdispls[i] = rdis;
      }
    } else {
      if (i % 2 == 1) {
        h_sendcounts[i] = 2 * count;
        h_recvcounts[i] = 2 * count;
        h_sdispls[i] = sdis;
        h_rdispls[i] = rdis;
        if (i == proc) {
          h_sendcounts[i] = 0;
          h_recvcounts[i] = 0;
        }
        sdis += 2 * count;
        rdis += 2 * count;
      } else {
        h_sendcounts[i] = 0;
        h_recvcounts[i] = 0;
        h_sdispls[i] = sdis;
        h_rdispls[i] = rdis;
      }
    }
  }
  for (int i = 0; i < num_warmup_iters; i++) {
    flagcxAlltoAllv(sendbuff, h_sendcounts, h_sdispls, recvbuff, h_recvcounts,
                    h_rdispls, DATATYPE, comm, stream);
  }
  devHandle->streamSynchronize(stream);

  // Warm-up for small size
  sdis = 0;
  rdis = 0;
  count = (min_bytes / sizeof(float)) / totalProcs;
  for (int i = 0; i < totalProcs; i++) {
    if (proc % 2 == 0) {
      if (i % 2 == 0) {
        h_sendcounts[i] = 2 * count;
        h_recvcounts[i] = 2 * count;
        h_sdispls[i] = sdis;
        h_rdispls[i] = rdis;
        if (i == proc) {
          h_sendcounts[i] = 0;
          h_recvcounts[i] = 0;
        }
        sdis += 2 * count;
        rdis += 2 * count;
      } else {
        h_sendcounts[i] = 0;
        h_recvcounts[i] = 0;
        h_sdispls[i] = sdis;
        h_rdispls[i] = rdis;
      }
    } else {
      if (i % 2 == 1) {
        h_sendcounts[i] = 2 * count;
        h_recvcounts[i] = 2 * count;
        h_sdispls[i] = sdis;
        h_rdispls[i] = rdis;
        if (i == proc) {
          h_sendcounts[i] = 0;
          h_recvcounts[i] = 0;
        }
        sdis += 2 * count;
        rdis += 2 * count;
      } else {
        h_sendcounts[i] = 0;
        h_recvcounts[i] = 0;
        h_sdispls[i] = sdis;
        h_rdispls[i] = rdis;
      }
    }
  }
  for (int i = 0; i < num_warmup_iters; i++) {
    flagcxAlltoAllv(sendbuff, h_sendcounts, h_sdispls, recvbuff, h_recvcounts,
                    h_rdispls, DATATYPE, comm, stream);
  }
  devHandle->streamSynchronize(stream);

  for (size_t size = min_bytes; size <= max_bytes; size *= step_factor) {
    sdis = 0;
    rdis = 0;
    count = (size / sizeof(float)) / totalProcs;

    for (int i = 0; i < totalProcs; i++) {
      ((float *)hello)[i * count] = 10 * proc + i;
    }

    devHandle->deviceMemcpy(sendbuff, hello, size, flagcxMemcpyHostToDevice,
                            NULL);

    if ((proc == 0 || proc == totalProcs - 1) && color == 0 && print_buffer) {
      printf("sendbuff = ");
      for (int i = 0; i < totalProcs; i++) {
        printf("%f ", ((float *)hello)[i * count]);
      }
      printf("\n");
    }

    for (int i = 0; i < totalProcs; i++) {
      if (proc % 2 == 0) {
        if (i % 2 == 0) {
          h_sendcounts[i] = 2 * count;
          h_recvcounts[i] = 2 * count;
          h_sdispls[i] = sdis;
          h_rdispls[i] = rdis;
          if (i == proc) {
            h_sendcounts[i] = 0;
            h_recvcounts[i] = 0;
          }
          sdis += 2 * count;
          rdis += 2 * count;
        } else {
          h_sendcounts[i] = 0;
          h_recvcounts[i] = 0;
          h_sdispls[i] = sdis;
          h_rdispls[i] = rdis;
        }
      } else {
        if (i % 2 == 1) {
          h_sendcounts[i] = 2 * count;
          h_recvcounts[i] = 2 * count;
          h_sdispls[i] = sdis;
          h_rdispls[i] = rdis;
          if (i == proc) {
            h_sendcounts[i] = 0;
            h_recvcounts[i] = 0;
          }
          sdis += 2 * count;
          rdis += 2 * count;
        } else {
          h_sendcounts[i] = 0;
          h_recvcounts[i] = 0;
          h_sdispls[i] = sdis;
          h_rdispls[i] = rdis;
        }
      }
    }

    if ((proc == 0 || proc == totalProcs - 1) && color == 0 && print_buffer) {
      printf("h_sendcounts = ");
      for (int i = 0; i < totalProcs; i++) {
        printf("%ld ", h_sendcounts[i]);
      }
      printf("\n");
      printf("h_recvcounts = ");
      for (int i = 0; i < totalProcs; i++) {
        printf("%ld ", h_recvcounts[i]);
      }
      printf("\n");
      printf("h_sdispls = ");
      for (int i = 0; i < totalProcs; i++) {
        printf("%ld ", h_sdispls[i]);
      }
      printf("\n");
      printf("h_rdispls = ");
      for (int i = 0; i < totalProcs; i++) {
        printf("%ld ", h_rdispls[i]);
      }
      printf("\n");
    }

    MPI_Barrier(MPI_COMM_WORLD);

    tim.reset();
    for (int i = 0; i < num_iters; i++) {
      flagcxAlltoAllv(sendbuff, h_sendcounts, h_sdispls, recvbuff, h_recvcounts,
                      h_rdispls, DATATYPE, comm, stream);
    }
    devHandle->streamSynchronize(stream);

    double elapsed_time = tim.elapsed() / num_iters;
    MPI_Allreduce(MPI_IN_PLACE, (void *)&elapsed_time, 1, MPI_DOUBLE, MPI_SUM,
                  MPI_COMM_WORLD);
    elapsed_time /= worldSize;

    double base_bw = (double)(size) / 1.0E9 / elapsed_time;
    double alg_bw = base_bw;
    double factor = ((double)(totalProcs - 1)) / ((double)(totalProcs));
    double bus_bw = base_bw * factor;
    if (proc == 0 && color == 0) {
      printf("Comm size: %zu bytes; Elapsed time: %lf sec; Algo bandwidth: %lf "
             "GB/s; Bus bandwidth: %lf GB/s\n",
             size, elapsed_time, alg_bw, bus_bw);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    devHandle->deviceMemset(hello, 0, size, flagcxMemHost, NULL);
    devHandle->deviceMemcpy(hello, recvbuff, size, flagcxMemcpyDeviceToHost,
                            NULL);
    if ((proc == 0 || proc == totalProcs - 1) && color == 0 && print_buffer) {
      printf("recvbuff = ");
      for (int i = 0; i < totalProcs; i++) {
        printf("%f ", ((float *)hello)[i * count]);
      }
      printf("\n");
    }
  }

  flagcxCommDestroy(comm);
  devHandle->streamDestroy(stream);
  devHandle->deviceFree(sendbuff, flagcxMemDevice, NULL);
  devHandle->deviceFree(recvbuff, flagcxMemDevice, NULL);
  devHandle->deviceFree(hello, flagcxMemHost, NULL);
  devHandle->deviceFree((void *)h_sendcounts, flagcxMemHost, NULL);
  devHandle->deviceFree((void *)h_recvcounts, flagcxMemHost, NULL);
  devHandle->deviceFree((void *)h_sdispls, flagcxMemHost, NULL);
  devHandle->deviceFree((void *)h_rdispls, flagcxMemHost, NULL);
  flagcxHandleFree(handler);

  MPI_Finalize();
  return 0;
}