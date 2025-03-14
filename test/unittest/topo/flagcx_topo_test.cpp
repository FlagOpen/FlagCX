#include "flagcx_topo_test.hpp"
#include <iostream>

void FlagCXTopoTest::SetUp() {
  FlagCXTest::SetUp();
  std::cout << "rank = " << rank << "; nranks = " << nranks << std::endl;

  // initialize flagcx handles
  flagcxHandleInit(&handler);
  flagcxUniqueId_t &uniqueId = handler->uniqueId;
  flagcxDeviceHandle_t &devHandle = handler->devHandle;

  int numDevices;
  devHandle->getDeviceCount(&numDevices);
  devHandle->setDevice(rank % numDevices);

  if (rank == 0)
    flagcxGetUniqueId(&uniqueId);
  std::cout << "finished getting uniqueId" << std::endl;
  MPI_Bcast((void *)uniqueId, sizeof(flagcxUniqueId), MPI_BYTE, 0,
            MPI_COMM_WORLD);
  MPI_Barrier(MPI_COMM_WORLD);

  /*
  we don't initialize communicator here
  because topology detection is part of communicator initialization
  */
}

void FlagCXTopoTest::TearDown() {
  flagcxComm_t &comm = handler->comm;
  flagcxCommDestroy(comm);

  flagcxHandleFree(handler);
}
