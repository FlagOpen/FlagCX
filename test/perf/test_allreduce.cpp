#include "flagcx.h"
#include "mpi.h"
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

  int totalProcs, proc;
  MPI_Init(&argc, &argv);
  MPI_Comm_size(MPI_COMM_WORLD, &totalProcs);
  MPI_Comm_rank(MPI_COMM_WORLD, &proc);
  printf("I am %d of %d\n", proc, totalProcs);

  flagcxHandlerGroup_t handler;
  flagcxHandleInit(&handler);
  flagcxUniqueId_t &uniqueId = handler->uniqueId;
  flagcxComm_t &comm = handler->comm;
  flagcxDeviceHandle_t &devHandle = handler->devHandle;

  int nGpu;
  devHandle->getDeviceCount(&nGpu);
  devHandle->setDevice(proc % nGpu);

  if (proc == 0)
    flagcxGetUniqueId(&uniqueId);
  MPI_Bcast((void *)uniqueId, sizeof(flagcxUniqueId), MPI_BYTE, 0,
            MPI_COMM_WORLD);
  MPI_Barrier(MPI_COMM_WORLD);

  flagcxCommInitRank(&comm, totalProcs, uniqueId, proc);

  flagcxStream_t stream;
  devHandle->streamCreate(&stream);

  void *sendbuff, *recvbuff, *hello;
  size_t count;
  timer tim;

  devHandle->deviceMalloc(&sendbuff, max_bytes, flagcxMemDevice, NULL);
  devHandle->deviceMalloc(&recvbuff, max_bytes, flagcxMemDevice, NULL);
  devHandle->deviceMalloc(&hello, max_bytes, flagcxMemHost, NULL);
  devHandle->deviceMemset(hello, 0, max_bytes, flagcxMemHost, NULL);

  // Warm-up for large size
  for (int i = 0; i < num_warmup_iters; i++) {
    flagcxAllReduce(sendbuff, recvbuff, max_bytes / sizeof(float), DATATYPE,
                    flagcxSum, comm, stream);
  }
  devHandle->streamSynchronize(stream);

  // Warm-up for small size
  for (int i = 0; i < num_warmup_iters; i++) {
    flagcxAllReduce(sendbuff, recvbuff, min_bytes / sizeof(float), DATATYPE,
                    flagcxSum, comm, stream);
  }
  devHandle->streamSynchronize(stream);

  for (size_t size = min_bytes; size <= max_bytes; size *= step_factor) {
    count = size / sizeof(float);

    for (size_t i = 0; i < count; i++) {
      ((float *)hello)[i] = i % 10;
    }

    devHandle->deviceMemcpy(sendbuff, hello, size, flagcxMemcpyHostToDevice,
                            NULL);

    if (proc == 0 && print_buffer) {
      printf("sendbuff = ");
      for (size_t i = 0; i < 10; i++) {
        printf("%f ", ((float *)hello)[i]);
      }
      printf("\n");
    }

    MPI_Barrier(MPI_COMM_WORLD);

    tim.reset();
    for (int i = 0; i < num_iters; i++) {
      flagcxAllReduce(sendbuff, recvbuff, count, DATATYPE, flagcxSum, comm,
                      stream);
    }
    devHandle->streamSynchronize(stream);

    double elapsed_time = tim.elapsed() / num_iters;
    MPI_Allreduce(MPI_IN_PLACE, (void *)&elapsed_time, 1, MPI_DOUBLE, MPI_SUM,
                  MPI_COMM_WORLD);
    elapsed_time /= totalProcs;

    double base_bw = (double)(size) / 1.0E9 / elapsed_time;
    double alg_bw = base_bw;
    double factor = ((double)(2 * (totalProcs - 1))) / ((double)(totalProcs));
    double bus_bw = base_bw * factor;
    if (proc == 0) {
      printf("Comm size: %zu bytes; Elapsed time: %lf sec; Algo bandwidth: %lf "
             "GB/s; Bus bandwidth: %lf GB/s\n",
             size, elapsed_time, alg_bw, bus_bw);
    }

    MPI_Barrier(MPI_COMM_WORLD);

    devHandle->deviceMemset(hello, 0, size, flagcxMemHost, NULL);
    devHandle->deviceMemcpy(hello, recvbuff, size, flagcxMemcpyDeviceToHost,
                            NULL);
    if (proc == 0 && print_buffer) {
      printf("recvbuff = ");
      for (size_t i = 0; i < 10; i++) {
        printf("%f ", ((float *)hello)[i]);
      }
      printf("\n");
    }
  }

  devHandle->deviceFree(sendbuff, flagcxMemDevice, NULL);
  devHandle->deviceFree(recvbuff, flagcxMemDevice, NULL);
  devHandle->deviceFree(hello, flagcxMemHost, NULL);
  devHandle->streamDestroy(stream);
  flagcxCommDestroy(comm);
  flagcxHandleFree(handler);

  MPI_Finalize();
  return 0;
}