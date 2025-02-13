#include "flagcx_coll_test.hpp"
#include <iostream>

void FlagCXCollTest::SetUp() {
    FlagCXTest::SetUp();
    std::cout << "rank = " << rank << "; nranks = " << nranks << std::endl;

    // initialize flagcx handles
    flagcxHandleInit(&handler);
    flagcxUniqueId_t& uniqueId = handler->uniqueId;
    flagcxComm_t& comm = handler->comm;
    flagcxDeviceHandle_t& devHandle = handler->devHandle;
    sendbuff = nullptr;
    recvbuff = nullptr;
    hostsendbuff = nullptr;
    hostrecvbuff = nullptr;
    size = 1ULL * 1024 * 1024 * 1024; // 1GB 
    count = size / sizeof(float);

    int numDevices;
    devHandle->getDeviceCount(&numDevices);
    devHandle->setDevice(rank % numDevices);

    // Create and broadcast uniqueId
    if (rank == 0)
        flagcxGetUniqueId(&uniqueId);
    MPI_Bcast((void*)uniqueId, sizeof(flagcxUniqueId), MPI_BYTE, 0, MPI_COMM_WORLD);
    MPI_Barrier(MPI_COMM_WORLD);

    // Create comm and stream
    flagcxCommInitRank(&comm, nranks, uniqueId, rank);
    devHandle->streamCreate(&stream);

    // allocate data and set inital value
    devHandle->deviceMalloc(&sendbuff, size, flagcxMemDevice);
    devHandle->deviceMalloc(&recvbuff, size, flagcxMemDevice);
    devHandle->deviceMalloc(&hostsendbuff, size, flagcxMemHost);
    devHandle->deviceMemset(hostsendbuff, 0, size, flagcxMemHost, NULL);
    devHandle->deviceMalloc(&hostrecvbuff, size, flagcxMemHost);
    devHandle->deviceMemset(hostrecvbuff, 0, size, flagcxMemHost, NULL);
}

void FlagCXCollTest::TearDown() {
    // destroy comm 
    flagcxComm_t& comm = handler->comm;
    flagcxCommDestroy(comm);

    // destroy stream
    flagcxDeviceHandle_t& devHandle = handler->devHandle;
    devHandle->streamDestroy(stream);

    // free data
    devHandle->deviceFree(sendbuff, flagcxMemDevice);
    devHandle->deviceFree(recvbuff, flagcxMemDevice);
    devHandle->deviceFree(hostsendbuff, flagcxMemHost);
    devHandle->deviceFree(hostrecvbuff, flagcxMemHost);

    // free handles
    flagcxHandleFree(handler);

    FlagCXTest::TearDown();
}

void FlagCXCollTest::Run() {}
