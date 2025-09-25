#include "reg_pool.h"
#include <cstdio>

#define DEFAULT_REGPOOL_SIZE 16

flagcxRegPool::flagcxRegPool() {
  pthread_mutex_init(&poolMutex, nullptr);
  pageSize = sysconf(_SC_PAGESIZE);
}

flagcxRegPool::~flagcxRegPool() {
  pthread_mutex_destroy(&poolMutex);
  regMap.clear();
  regPool.clear();
}

inline void flagcxRegPool::getPagedAddr(void *data, size_t length,
                                        uintptr_t *beginAddr,
                                        uintptr_t *endAddr) {
  *beginAddr = reinterpret_cast<uintptr_t>(data) & -pageSize;
  *endAddr =
      (reinterpret_cast<uintptr_t>(data) + length + pageSize - 1) & -pageSize;
}

flagcxResult_t flagcxRegPool::removeRegItemNetHandles(void *comm,
                                                      flagcxRegItem *reg) {
  if (comm == nullptr || reg == nullptr)
    return flagcxSuccess;

  for (auto it = reg->netHandles.begin(); it != reg->netHandles.end();) {
    FLAGCXCHECK(flagcxNetDeregisterBuffer(comm, it->proxyConn, it->handle));
    it = reg->netHandles.erase(it);
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxRegPool::registerBuffer(void *comm, void *data,
                                             size_t length) {
  if (comm == nullptr || data == nullptr || length == 0)
    return flagcxSuccess;

  uintptr_t commKey = reinterpret_cast<uintptr_t>(comm);
  uintptr_t beginAddr, endAddr;
  getPagedAddr(data, length, &beginAddr, &endAddr);

  pthread_mutex_lock(&poolMutex);

  auto &regCommPool = regPool[commKey];
  for (auto it = regCommPool.begin(); it != regCommPool.end(); it++) {
    if (beginAddr < it->beginAddr) {
      flagcxRegItem reg{beginAddr, endAddr, 1, {}};
      auto &insertedReg = *regCommPool.insert(it, std::move(reg));
      regMap[commKey][reinterpret_cast<uintptr_t>(data)] = &insertedReg;
      pthread_mutex_unlock(&poolMutex);
      return flagcxSuccess;
    } else if (it->beginAddr <= beginAddr && it->endAddr >= endAddr) {
      it->refCount++;
      pthread_mutex_unlock(&poolMutex);
      return flagcxSuccess;
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxRegPool::deRegisterBuffer(void *comm, void *handle) {
  if (comm == nullptr || handle == nullptr)
    return flagcxSuccess;

  uintptr_t commKey = reinterpret_cast<uintptr_t>(comm);
  flagcxRegItem *reg = (flagcxRegItem *)handle;
  pthread_mutex_lock(&poolMutex);

  auto &regCommPool = regPool[commKey];
  for (auto it = regCommPool.begin(); it != regCommPool.end(); it++) {
    if (&(*it) == reg) {
      it->refCount--;
      if (it->refCount > 0) {
        pthread_mutex_unlock(&poolMutex);
        return flagcxSuccess;
      }
      FLAGCXCHECK(removeRegItemNetHandles(comm, reg));
      auto &regCommMap = regMap[commKey];
      for (auto mapIter = regCommMap.begin(); mapIter != regCommMap.end();) {
        if (mapIter->second == reg) {
          mapIter = regCommMap.erase(mapIter);
        } else {
          mapIter++;
        }
      }
      regCommPool.erase(it);
      pthread_mutex_unlock(&poolMutex);
      return flagcxSuccess;
    }
  }

  pthread_mutex_unlock(&poolMutex);
  WARN("Could not find the given handle in regPool");
  return flagcxInvalidUsage;
}

// void flagcxRegPool::updateBuffer(void *data, int newStatus) {
//   if (data == nullptr) return;

//   uintptr_t beginAddr;
//   getPagedAddr(data, 0, &beginAddr, nullptr);

//   pthread_mutex_lock(&poolMutex);

//   auto it = regMap.upper_bound(beginAddr);
//   if (it == regMap.begin()) {
//     pthread_mutex_unlock(&poolMutex);
//     return;
//   }
//   --it; // now it points to interval whose start <= beginAddr

//   if (!(beginAddr >= it->first && beginAddr < it->second.endAddr)) {
//     pthread_mutex_unlock(&poolMutex);
//     return;
//   }

//   if (it->second.status == newStatus) {
//     pthread_mutex_unlock(&poolMutex);
//     return;
//   }

//   uintptr_t start = it->first;
//   uintptr_t end = it->second.endAddr;

//   flagcxRegItem old = it->second;
//   regMap.erase(it);

//   if (start < beginAddr) {
//     regMap[start] = {beginAddr, old.type, old.status, old.sendMrHandle,
//     old.recvMrHandle};
//   }
//   regMap[beginAddr] = {end, old.type, newStatus, old.sendMrHandle,
//   old.recvMrHandle};

// //   auto cur = regMap.find(beginAddr);
// //   auto next = std::next(cur);
// //   if (next != regMap.end() && next->second.type == cur->second.type) {
// //     cur->second.endAddr = next->second.endAddr;
// //     regMap.erase(next);
// //   }

// //   if (cur != regMap.begin()) {
// //     auto prev = std::prev(cur);
// //     if (prev->second.type == cur->second.type &&
// //         prev->second.endAddr == cur->first) {
// //       prev->second.endAddr = cur->second.endAddr;
// //       regMap.erase(cur);
// //     }
// //   }

//   pthread_mutex_unlock(&poolMutex);
// }

// uintptr_t flagcxRegPool::findBuffer(void *data, size_t length) {
//   if (data == nullptr || length == 0) return 0;

//   uintptr_t beginAddr, endAddr;
//   getPagedAddr(data, length, &beginAddr, &endAddr);

//   pthread_mutex_lock(&poolMutex);

//   auto it = regMap.lower_bound(beginAddr);
//   if (it != regMap.begin())
//     --it;

//   for (; it != regMap.end() && it->first < endAddr; ++it) {
//     uintptr_t existBegin = it->first;
//     uintptr_t existEnd = it->second.endAddr;

//     if (existEnd <= beginAddr)
//       continue;
//     if (existBegin >= endAddr)
//       break;

//     if (beginAddr >= existBegin && endAddr <= existEnd) {
//       pthread_mutex_unlock(&poolMutex);
//       return existBegin;
//     }
//   }

//   pthread_mutex_unlock(&poolMutex);
//   return 0;
// }

std::map<uintptr_t, std::map<uintptr_t, flagcxRegItem *>> &
flagcxRegPool::getGlobalMap() {
  return regMap;
}

flagcxRegItem *flagcxRegPool::getItem(const void *comm, void *data) {
  uintptr_t commKey = reinterpret_cast<uintptr_t>(comm);
  uintptr_t key = reinterpret_cast<uintptr_t>(data);
  auto it = regMap[commKey].find(key);
  if (it == regMap[commKey].end()) {
    return nullptr;
  }
  return it->second;
}

void flagcxRegPool::dump() {
  pthread_mutex_lock(&poolMutex);
  printf("========================\n");
  printf("RegPool(pageSize=%lu\n", pageSize);
  for (auto &c : regMap) {
    printf("==comm(%lu)==\n", c.first);
    for (auto &p : c.second) {
      printf("%lu -> regItem[%lu,%lu,%d]\n", p.first, p.second->beginAddr,
             p.second->endAddr, p.second->refCount);
      auto it = p.second->netHandles.begin();
      for (; it != p.second->netHandles.end(); it++) {
        printf("%p -> netHandle[%p,%p]\n", &(*it), it->handle, it->proxyConn);
      }
    }
    printf("==comm(%lu)==\n", c.first);
  }
  printf("========================\n");
  pthread_mutex_unlock(&poolMutex);
}