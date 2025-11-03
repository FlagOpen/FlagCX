#include "launch_kernel.h"
#include "group.h"
#include <stdio.h>

flagcxLaunchFunc_t deviceAsyncKernel = NULL;

flagcxResult_t loadKernelSymbol(const char *path, const char *name,
                                flagcxLaunchFunc_t *fn) {
  void *handle = flagcxOpenLib(
      path, RTLD_LAZY, [](const char *p, int err, const char *msg) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
      });

  if (!handle)
    return flagcxSystemError;

  void *sym = dlsym(handle, name);
  if (!sym) {
    fprintf(stderr, "dlsym failed: %s\n", dlerror());
    return flagcxSystemError;
  }

  *fn = (flagcxLaunchFunc_t)sym;
  return flagcxSuccess;
}

void cpuAsyncKernel(void *args) {
  flagcxHostSemaphore *semaphore = (flagcxHostSemaphore *)args;
  semaphore->signalStart();
  semaphore->wait();
  semaphore->signalEnd();
}