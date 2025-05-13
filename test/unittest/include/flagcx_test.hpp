#pragma once

#include "mpi.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>

class MPIEnvironment : public ::testing::Environment {
public:
  virtual void SetUp() {
    char **argv;
    int argc = 0;
    int mpiError = MPI_Init(&argc, &argv);
    ASSERT_FALSE(mpiError);
  }

  virtual void TearDown() {
    int mpiError = MPI_Finalize();
    ASSERT_FALSE(mpiError);
  }

  virtual ~MPIEnvironment() {}
};

class FlagCXTest : public testing::Test {
protected:
  void SetUp() override;

  void TearDown() override {}

  void Run() {}

  int rank;
  int nranks;
  // static Parser parser;
  std::string type;
};
