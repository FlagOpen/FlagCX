#include "flagcx_coll_test.hpp"
#include <string.h>

TEST_F(FlagCXCollTest, AllReduce) {
    flagcxComm_t& comm = handler->comm;
    flagcxDeviceHandle_t& devHandle = handler->devHandle;
    // initialize data
    for (size_t i = 0; i < count; i++) {
        ((float*)hostsendbuff)[i] = i % 10;
    }

    devHandle->deviceMemcpy(sendbuff, hostsendbuff, size, flagcxMemcpyHostToDevice, NULL);

    if (rank == 0) {
        std::cout << "sendbuff = ";
        for (size_t i = 0; i < 10; i++) {
            std::cout << ((float*)hostsendbuff)[i] << " ";
        }
        std::cout << ((float*)hostsendbuff)[10] << std::endl;
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // run allreduce and sync stream
    flagcxAllReduce(sendbuff, recvbuff, count, flagcxFloat, flagcxSum, comm, stream);
    devHandle->streamSynchronize(stream);

    // copy recvbuff back to hostrecvbuff
    devHandle->deviceMemcpy(hostrecvbuff, recvbuff, size, flagcxMemcpyDeviceToHost, NULL);

    // divide by nranks
    for (size_t i = 0; i < count; i++) {
        ((float*)hostrecvbuff)[i] /= nranks;
    }

    if (rank == 0) {
        std::cout << "recvbuff = ";
        for (size_t i = 0; i < 10; i++) {
            std::cout << ((float*)hostrecvbuff)[i] << " ";
        }
        std::cout << ((float*)hostrecvbuff)[10] << std::endl;
    }

    MPI_Barrier(MPI_COMM_WORLD);

    EXPECT_EQ(strcmp(static_cast<char*>(hostsendbuff), static_cast<char*>(hostrecvbuff)), 0);
}

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new MPIEnvironment);
    return RUN_ALL_TESTS();
}
