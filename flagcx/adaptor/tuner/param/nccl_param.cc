// This is a example of intercept NCCL environment getter functions.
#include <stdio.h>
#include <cstring>
#include <string>
#include <cstdint>
#include <pthread.h>
#include <stdlib.h>  // for setenv, getenv
#include <iostream>

static void ncclLoadParam(char const* env, int64_t deftVal, int64_t* value) {
  static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
  pthread_mutex_lock(&mutex);
  const char* str = getenv(env);
  *value = deftVal;
  if (str && strlen(str) > 0) {
    try {
      *value = std::stoll(str);
    } catch (const std::exception& e) {
      *value = deftVal;
    }
  }
  pthread_mutex_unlock(&mutex);
}

#define NCCL_PARAM(name, env, deftVal)		 \
  int64_t ncclParam##name() {			 \
    int64_t value = INT64_MIN;			 \
    ncclLoadParam("NCCL_" env, deftVal, &value); \
    std::cout<<"========="<<env<<" is set to "<<value<<std::endl; \
    return value;				 \
  }

// In order to intercept a NCCL environment getter function, add an additional line of NCCL_PARAM here.
// Below is an example of intercepting NCCL_P2P_NVL_CHUNKSIZE / NTHREADS / MIN_NCHANNELS env.

NCCL_PARAM(P2pNvlChunkSize, "P2P_NVL_CHUNKSIZE", (1 << 19)); /* 512 kB */
NCCL_PARAM(Nthreads, "NTHREADS", (1 << 8)); /*256*/
NCCL_PARAM(MinNchannels, "MIN_NCHANNELS", (1 << 10)); /*1024*/