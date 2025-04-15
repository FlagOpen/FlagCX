#define CREATE_DEVICE_TOPO_API
#include "adaptor.h"
#include "debug.h"
#include "param.h"
#include "topo.h"
#include "utils.h"
#include <dlfcn.h>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <math.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

using namespace std;

struct flagcxDeviceAdaptor devRunTimeApi;

void *dlsymCheck(void *handle, const char *funcName) {
  void *funcPtr = dlsym(handle, funcName);
  if (funcPtr == NULL)
    INFO(FLAGCX_INIT, "fail to load symbol %s", funcName);
  return funcPtr;
}

#define LOADSYMBOL(handle, api)                                                \
  do {                                                                         \
    api = (typeof(api))dlsymCheck(handle, #api);                               \
  } while (0);
