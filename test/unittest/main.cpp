#include "flagcx_coll_test.hpp"
#include "flagcx_topo_test.hpp"
#include <string.h>
#include <fstream>
#include <vector>
#include <iostream>

#define BASELINE_FILE "baseline_result.txt"
#define NUM_BASELINE_ENTRIES 1000

TEST_F(FlagCXCollTest, AllReduce) {
  flagcxComm_t &comm = handler->comm;
  flagcxDeviceHandle_t &devHandle = handler->devHandle;

  for (size_t i = 0; i < count; i++) {
    ((float *)hostsendbuff)[i] = i % 10;
  }

  devHandle->deviceMemcpy(sendbuff, hostsendbuff, size,
                          flagcxMemcpyHostToDevice, NULL);

  if (rank == 0) {
    std::cout << "sendbuff = ";
    for (size_t i = 0; i < 10; i++) {
      std::cout << ((float *)hostsendbuff)[i] << " ";
    }
    std::cout << ((float *)hostsendbuff)[10] << std::endl;
  }

  MPI_Barrier(MPI_COMM_WORLD);

  flagcxAllReduce(sendbuff, recvbuff, count, flagcxFloat, flagcxSum, comm,
                  stream);
  devHandle->streamSynchronize(stream);

  devHandle->deviceMemcpy(hostrecvbuff, recvbuff, size,
                          flagcxMemcpyDeviceToHost, NULL);

  for (size_t i = 0; i < count; i++) {
    ((float *)hostrecvbuff)[i] /= nranks;
  }

  if (rank == 0) {
    std::cout << "recvbuff = ";
    for (size_t i = 0; i < 10; i++) {
      std::cout << ((float *)hostrecvbuff)[i] << " ";
    }
    std::cout << ((float *)hostrecvbuff)[10] << std::endl;
  }

  MPI_Barrier(MPI_COMM_WORLD);

  if (rank == 0 && !std::ifstream(BASELINE_FILE)) {
    std::ofstream baseline_file(BASELINE_FILE);
    for (size_t i = 0; i < NUM_BASELINE_ENTRIES; i++) {
      baseline_file << ((float *)hostrecvbuff)[i] << "\n";
    }
    baseline_file.close();
    std::cout << "Baseline results saved to " << BASELINE_FILE << std::endl;
  }

  std::vector<float> baseline_result(NUM_BASELINE_ENTRIES);
  std::ifstream baseline_file(BASELINE_FILE);
  for (size_t i = 0; i < NUM_BASELINE_ENTRIES; i++) {
    baseline_file >> baseline_result[i];
  }
  baseline_file.close();

  for (size_t i = 0; i < NUM_BASELINE_ENTRIES; i++) {
    ASSERT_FLOAT_EQ(((float *)hostrecvbuff)[i], baseline_result[i])
        << "Mismatch at index " << i
        << ": Expected " << baseline_result[i]
        << ", Got " << ((float *)hostrecvbuff)[i];
  }
}

TEST_F(FlagCXCollTest, AllGather) {
  flagcxComm_t &comm = handler->comm;
  flagcxDeviceHandle_t &devHandle = handler->devHandle;

  for (size_t i = 0; i < count; i++) {
    ((float *)hostsendbuff)[i] = i % 10;
  }

  devHandle->deviceMemcpy(sendbuff, hostsendbuff, size,
                          flagcxMemcpyHostToDevice, NULL);

  if (rank == 0) {
    std::cout << "sendbuff = ";
    for (size_t i = 0; i < 10; i++) {
      std::cout << ((float *)hostsendbuff)[i] << " ";
    }
    std::cout << ((float *)hostsendbuff)[10] << std::endl;
  }

  MPI_Barrier(MPI_COMM_WORLD);


  devHandle->deviceMalloc(&recvbuff, size * nranks, flagcxMemDevice, NULL);

  flagcxAllGather(sendbuff, recvbuff, count, flagcxFloat, comm, stream);
  devHandle->streamSynchronize(stream);

  devHandle->deviceMemcpy(hostrecvbuff, recvbuff, size * nranks,
                          flagcxMemcpyDeviceToHost, NULL);

  if (rank == 0) {
    std::cout << "recvbuff = ";
    for (size_t i = 0; i < 10; i++) {
      std::cout << ((float *)hostrecvbuff)[i] << " ";
    }
    std::cout << ((float *)hostrecvbuff)[10] << std::endl;
  }

  MPI_Barrier(MPI_COMM_WORLD);

  EXPECT_EQ(strcmp(static_cast<char *>(hostsendbuff),
                   static_cast<char *>(hostrecvbuff)),
            0);
}


TEST_F(FlagCXCollTest, ReduceScatter) {
  flagcxComm_t &comm = handler->comm;
  flagcxDeviceHandle_t &devHandle = handler->devHandle;

  for (size_t i = 0; i < count; i++) {
    ((float *)hostsendbuff)[i] = i % 10;
  }

  devHandle->deviceMemcpy(sendbuff, hostsendbuff, size,
                          flagcxMemcpyHostToDevice, NULL);

  if (rank == 0) {
    std::cout << "sendbuff = ";
    for (size_t i = 0; i < 10; i++) {
      std::cout << ((float *)hostsendbuff)[i] << " ";
    }
    std::cout << ((float *)hostsendbuff)[10] << std::endl;
  }

  MPI_Barrier(MPI_COMM_WORLD);


  flagcxReduceScatter(sendbuff, recvbuff, count / nranks, flagcxFloat, flagcxSum, comm, stream);
  devHandle->streamSynchronize(stream);

  devHandle->deviceMemcpy(hostrecvbuff, recvbuff, size / nranks,
                          flagcxMemcpyDeviceToHost, NULL);

  if (rank == 0) {
    std::cout << "recvbuff = ";
    for (size_t i = 0; i < 10; i++) {
      std::cout << ((float *)hostrecvbuff)[i] << " ";
    }
    std::cout << ((float *)hostrecvbuff)[10] << std::endl;
  }

  MPI_Barrier(MPI_COMM_WORLD);

  EXPECT_EQ(strcmp(static_cast<char *>(hostsendbuff),
                   static_cast<char *>(hostrecvbuff)),
            0);
}


TEST_F(FlagCXCollTest, Reduce) {
  flagcxComm_t &comm = handler->comm;
  flagcxDeviceHandle_t &devHandle = handler->devHandle;

  for (size_t i = 0; i < count; i++) {
    ((float *)hostsendbuff)[i] = i % 10;
  }

  devHandle->deviceMemcpy(sendbuff, hostsendbuff, size,
                          flagcxMemcpyHostToDevice, NULL);

  if (rank == 0) {
    std::cout << "sendbuff = ";
    for (size_t i = 0; i < 10; i++) {
      std::cout << ((float *)hostsendbuff)[i] << " ";
    }
    std::cout << ((float *)hostsendbuff)[10] << std::endl;
  }

  MPI_Barrier(MPI_COMM_WORLD);

  flagcxReduce(sendbuff, recvbuff, count, flagcxFloat, flagcxSum, 0, comm, stream);
  devHandle->streamSynchronize(stream);

  devHandle->deviceMemcpy(hostrecvbuff, recvbuff, size,
                          flagcxMemcpyDeviceToHost, NULL);

  if (rank == 0) {
    std::cout << "recvbuff = ";
    for (size_t i = 0; i < 10; i++) {
      std::cout << ((float *)hostrecvbuff)[i] << " ";
    }
    std::cout << ((float *)hostrecvbuff)[10] << std::endl;
  }

  MPI_Barrier(MPI_COMM_WORLD);

  EXPECT_EQ(strcmp(static_cast<char *>(hostsendbuff),
                   static_cast<char *>(hostrecvbuff)),
            0);
}


TEST_F(FlagCXCollTest, Gather) {
  flagcxComm_t &comm = handler->comm;
  flagcxDeviceHandle_t &devHandle = handler->devHandle;


  for (size_t i = 0; i < count; i++) {
    static_cast<float *>(hostsendbuff)[i] = static_cast<float>(i % 10);
  }


  devHandle->deviceMemcpy(sendbuff, hostsendbuff, size,
                          flagcxMemcpyHostToDevice, NULL);


  if (rank == 0) {
    std::cout << "sendbuff  = ";
    for (size_t i = 0; i < 10 && i < count; i++) {
      std::cout << static_cast<float *>(hostsendbuff)[i] << " ";
    }
    std::cout << std::endl;
  }

  MPI_Barrier(MPI_COMM_WORLD);


  devHandle->deviceMalloc(&recvbuff, size * nranks, flagcxMemDevice, NULL);


  flagcxGather(sendbuff, recvbuff, count, flagcxFloat, 0, comm, stream);
  devHandle->streamSynchronize(stream);


  devHandle->deviceMemcpy(hostrecvbuff, recvbuff, size * nranks,
                          flagcxMemcpyDeviceToHost, NULL);

  if (rank == 0) {
    std::cout << "recvbuff  = ";
    for (size_t i = 0; i < 10 && i < count * nranks; i++) {
      std::cout << static_cast<float *>(hostrecvbuff)[i] << " ";
    }
    std::cout << std::endl;
  }

  MPI_Barrier(MPI_COMM_WORLD);

}

TEST_F(FlagCXCollTest, Scatter) {
  flagcxComm_t &comm = handler->comm;
  flagcxDeviceHandle_t &devHandle = handler->devHandle;

  size_t slice_count = count / nranks;
  size_t slice_size = slice_count * sizeof(float);

  if (rank == 0) {
    for (size_t i = 0; i < count; i++) {
      ((float *)hostsendbuff)[i] = static_cast<float>(i);
    }

    devHandle->deviceMemcpy(sendbuff, hostsendbuff, size,
                            flagcxMemcpyHostToDevice, NULL);

    std::cout << "sendbuff = ";
    for (size_t i = 0; i < 10 && i < count; i++) {
      std::cout << ((float *)hostsendbuff)[i] << " ";
    }
    std::cout << std::endl;
  }

  MPI_Barrier(MPI_COMM_WORLD);

  devHandle->deviceMalloc(&recvbuff, slice_size, flagcxMemDevice, NULL);
  flagcxScatter(sendbuff, recvbuff, slice_count, flagcxFloat, 0, comm, stream);
  devHandle->streamSynchronize(stream);

  devHandle->deviceMemcpy(hostrecvbuff, recvbuff, slice_size,
                          flagcxMemcpyDeviceToHost, NULL);

  if (rank == 0) {
    std::cout << "recvbuff = ";
    for (size_t i = 0; i < 10 && i < slice_count; i++) {
      std::cout << ((float *)hostrecvbuff)[i] << " ";
    }
    std::cout << std::endl;
  }

  MPI_Barrier(MPI_COMM_WORLD);
  EXPECT_EQ(strcmp(static_cast<char *>(hostsendbuff),
                   static_cast<char *>(hostrecvbuff)),
            0);


}


TEST_F(FlagCXCollTest, Broadcast) {
  flagcxComm_t &comm = handler->comm;
  flagcxDeviceHandle_t &devHandle = handler->devHandle;

  for (size_t i = 0; i < count; i++) {
    ((float *)hostsendbuff)[i] = i % 10;
  }

  devHandle->deviceMemcpy(sendbuff, hostsendbuff, size,
                          flagcxMemcpyHostToDevice, NULL);

  if (rank == 0) {
    std::cout << "sendbuff = ";
    for (size_t i = 0; i < 10; i++) {
      std::cout << ((float *)hostsendbuff)[i] << " ";
    }
    std::cout << ((float *)hostsendbuff)[10] << std::endl;
  }

  MPI_Barrier(MPI_COMM_WORLD);

  flagcxBroadcast(sendbuff, recvbuff, count, flagcxFloat, 0, comm, stream);
  devHandle->streamSynchronize(stream);

  devHandle->deviceMemcpy(hostrecvbuff, recvbuff, size,
                          flagcxMemcpyDeviceToHost, NULL);

  if (rank == 0) {
    std::cout << "recvbuff = ";
    for (size_t i = 0; i < 10; i++) {
      std::cout << ((float *)hostrecvbuff)[i] << " ";
    }
    std::cout << ((float *)hostrecvbuff)[10] << std::endl;
  }

  MPI_Barrier(MPI_COMM_WORLD);

  EXPECT_EQ(strcmp(static_cast<char *>(hostsendbuff),
                   static_cast<char *>(hostrecvbuff)),
            0);
}




TEST_F(FlagCXTopoTest, TopoDetection) {
  flagcxComm_t &comm = handler->comm;
  flagcxUniqueId_t &uniqueId = handler->uniqueId;

  std::cout << "executing flagcxCommInitRank" << std::endl;
  auto result = flagcxCommInitRank(&comm, nranks, uniqueId, rank);
  EXPECT_EQ(result, flagcxSuccess);
}

int main(int argc, char *argv[]) {
  ::testing::InitGoogleTest(&argc, argv);
  ::testing::AddGlobalTestEnvironment(new MPIEnvironment);
  return RUN_ALL_TESTS();
}

