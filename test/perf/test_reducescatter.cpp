#include "mpi.h"
#include "flagcx.h"
#include "tools.h"
#include <iostream>
#include <cstring>

#define DATATYPE flagcxFloat

int main(int argc, char *argv[]){
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
    flagcxUniqueId_t& uniqueId = handler->uniqueId;
    flagcxComm_t& comm = handler->comm;
    flagcxDeviceHandle_t& devHandle = handler->devHandle;

    int nGpu;
    devHandle->getDeviceCount(&nGpu);
    devHandle->setDevice(proc % nGpu);

    if (proc == 0)
        flagcxGetUniqueId(&uniqueId);
    MPI_Bcast((void *)uniqueId, sizeof(flagcxUniqueId), MPI_BYTE, 0, MPI_COMM_WORLD);
    MPI_Barrier(MPI_COMM_WORLD);

    flagcxCommInitRank(&comm, totalProcs, uniqueId, proc);

    flagcxStream_t stream;
    devHandle->streamCreate(&stream);

    void *sendbuff, *recvbuff, *hello;
    size_t count, recvcount, recvsize;
    timer tim;
    
    for (size_t size = min_bytes; size <= max_bytes; size *= step_factor) {
        count = size / sizeof(float);
        recvcount = count / totalProcs;
        recvsize = size / totalProcs;
        devHandle->deviceMalloc(&sendbuff, size, flagcxMemDevice, NULL);
        devHandle->deviceMalloc(&recvbuff, recvsize, flagcxMemDevice, NULL);
        devHandle->deviceMalloc(&hello, size, flagcxMemHost, NULL);
        devHandle->deviceMemset(hello, 0, size, flagcxMemHost, NULL);

        size_t index = 0;
        float value = 0.0;
        for (size_t i = 0; i < count; i++) {
            ((float *)hello)[i] = value;
            if (index == recvcount - 1) {
                index = 0;
                value += 1.0;
            } else {
                index++;
            }
        }

        devHandle->deviceMemcpy(sendbuff, hello, size, flagcxMemcpyHostToDevice, NULL);
    
        if ((proc == 0 || proc == totalProcs - 1) && print_buffer) {
            printf("proc %d sendbuff = ", proc);
            for (size_t i = proc * recvcount; i < proc * recvcount + 10; i++) {
                printf("%f ", ((float *)hello)[i]);
            }
            printf("\n");
        }

        for(int i=0;i<num_warmup_iters;i++){
            flagcxReduceScatter(sendbuff, recvbuff, recvcount, DATATYPE, flagcxSum, comm, stream);
        }
        devHandle->streamSynchronize(stream);

        MPI_Barrier(MPI_COMM_WORLD);

        tim.reset();
        for(int i=0;i<num_iters;i++){
            flagcxReduceScatter(sendbuff, recvbuff, recvcount, DATATYPE, flagcxSum, comm, stream);
        }
        devHandle->streamSynchronize(stream);

        double elapsed_time = tim.elapsed() / num_iters;
        double base_bw = (double)(size) / 1.0E9 / elapsed_time;
        double alg_bw = base_bw;
        double factor = ((double)(totalProcs - 1))/((double)(totalProcs));
        double bus_bw = base_bw * factor;
        if (proc == 0) {
            printf("Comm size: %zu bytes; Elapsed time: %lf sec; Algo bandwidth: %lf GB/s; Bus bandwidth: %lf GB/s\n", size, elapsed_time, alg_bw, bus_bw);
        }

        MPI_Barrier(MPI_COMM_WORLD);

        devHandle->deviceMemset(hello, 0, size, flagcxMemHost, NULL);
        devHandle->deviceMemcpy(hello, recvbuff, recvsize, flagcxMemcpyDeviceToHost, NULL);
        if ((proc == 0 || proc == totalProcs - 1) && print_buffer) {
            printf("proc %d recvbuff = ", proc);
            for (size_t i = 0; i < 10; i++) {
                printf("%f ", ((float *)hello)[i]);
            }
            printf("\n");
        }

        devHandle->deviceFree(sendbuff, flagcxMemDevice, NULL);
        devHandle->deviceFree(recvbuff, flagcxMemDevice, NULL);
        devHandle->deviceFree(hello, flagcxMemHost, NULL);
    }

    devHandle->streamDestroy(stream);
    flagcxCommDestroy(comm);
    flagcxHandleFree(handler);

    MPI_Finalize();
    return 0;
} 
