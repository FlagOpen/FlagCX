#include "flagcx_test.hpp"

void FlagCXTest::SetUp()
{
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &nranks);
}
