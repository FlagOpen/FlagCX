#ifndef FLAGCX_LAUNCH_KERNEL_H_
#define FLAGCX_LAUNCH_KERNEL_H_

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

struct hostLaunchArgs {
  volatile bool stopLaunch;
  volatile bool retLaunch;
};

void cpuAsyncLaunch(void *_args);
void cpuStreamWait(void *_args);

#endif
