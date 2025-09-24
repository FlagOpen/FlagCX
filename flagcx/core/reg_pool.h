#ifndef FLAGCX_REGPOOL_H
#define FLAGCX_REGPOOL_H

#include "flagcx.h"
#include <list>
#include <map>
#include <pthread.h>
#include <unistd.h>

struct flagcxRegItem {
  uintptr_t beginAddr = 0;
  uintptr_t endAddr = 0;
  int refCount = 1;
  int status = 0; // 0:to-be-registered, 1:registered
  void *sendMrHandle = nullptr;
  void *recvMrHandle = nullptr;
};

class flagcxRegPool {
public:
  flagcxRegPool();
  ~flagcxRegPool();

  inline void getPagedAddr(void *data, size_t length, uintptr_t *beginAddr,
                           uintptr_t *endAddr);
  void registerBuffer(void *data, size_t length);
  void deRegisterBuffer(void *data);
  // void updateBuffer(void *data, int newStatus);
  // uintptr_t findBuffer(void *data, size_t length);
  std::map<uintptr_t, flagcxRegItem *> &getMap();
  flagcxRegItem *getItem(uintptr_t key);
  void dump();

private:
  std::map<uintptr_t, flagcxRegItem *> regMap;
  std::list<flagcxRegItem> regPool;
  pthread_mutex_t poolMutex;
  uintptr_t pageSize;
};

extern flagcxRegPool globalRegPool;

#endif // FLAGCX_REGPOOL_H