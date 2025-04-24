#ifndef FLAGCX_LAUNCH_KERNEL_H_
#define FLAGCX_LAUNCH_KERNEL_H_

#include "topo.h"
#include "debug.h"
#include <stdlib.h>
#include <getopt.h>
#include <memory.h>
#include <stdio.h>
#include <math.h>
#include <iostream>
#include <iomanip>
#include <unistd.h>
#include <dlfcn.h>
#include "adaptor.h"
#include "utils.h"
#include "param.h"

struct hostLaunchArgs{
    volatile bool stopLaunch;
    volatile bool retLaunch;
};

void cpuAsyncLaunch(void *_args);
void cpuStreamWait(void *_args);

#endif
