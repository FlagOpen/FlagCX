#include "launch_kernel.h"
#include "group.h"
#include <stdio.h>

flagcxLaunchFunc_t deviceAsyncLoad = NULL;
flagcxLaunchFunc_t deviceAsyncStore = NULL;

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

void cpuAsyncStore(void *args) {
  bool *volatile value = (bool *)args;
  __atomic_store_n(value, 1, __ATOMIC_RELAXED);
}

void cpuAsyncLoad(void *args) {
  bool *volatile value = (bool *)args;
  while (!__atomic_load_n(value, __ATOMIC_RELAXED)) {
  }
}