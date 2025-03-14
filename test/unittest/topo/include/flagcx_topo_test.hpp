#pragma once

#include "flagcx.h"
#include "flagcx_test.hpp"

class FlagCXTopoTest : public FlagCXTest {
protected:
  FlagCXTopoTest() {}

  void SetUp();

  void TearDown();

  void Run();

  flagcxHandlerGroup_t handler;
  flagcxStream_t stream;
  void *sendbuff;
  void *recvbuff;
  void *hostsendbuff;
  void *hostrecvbuff;
  size_t size;
  size_t count;
};