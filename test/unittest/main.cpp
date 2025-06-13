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

