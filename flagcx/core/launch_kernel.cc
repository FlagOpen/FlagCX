#include "launch_kernel.h"

void cpuStreamWait(void *_args) {
  bool *volatile args = (bool *)_args;
  __atomic_store_n(args, 1, __ATOMIC_RELAXED);
}

void cpuAsyncLaunch(void *_args) {
  bool *volatile args = (bool *)_args;
  while (!__atomic_load_n(args, __ATOMIC_RELAXED))
    ;
  free(args);
}
