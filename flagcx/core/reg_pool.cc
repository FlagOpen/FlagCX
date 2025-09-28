#include "reg_pool.h"
#include <cstdio>

#define DEFAULT_REGPOOL_SIZE 16

flagcxRegPool::flagcxRegPool() { pageSize = sysconf(_SC_PAGESIZE); }

flagcxRegPool::~flagcxRegPool() {
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
  if (comm == nullptr || reg == nullptr) {
    return flagcxSuccess;
  }

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

  auto &regCommPool = regPool[commKey];
  for (auto it = regCommPool.begin(); it != regCommPool.end(); it++) {
    // found a place to insert
    if (beginAddr < it->beginAddr) {
      flagcxRegItem reg{beginAddr, endAddr, 1, {}};
      auto &insertedReg = *regCommPool.insert(it, std::move(reg));
      regMap[commKey][reinterpret_cast<uintptr_t>(data)] = &insertedReg;
      return flagcxSuccess;
      // already inserted, just increase ref count
    } else if (it->beginAddr <= beginAddr && it->endAddr >= endAddr) {
      it->refCount++;
      return flagcxSuccess;
    }
  }

  // not found, insert to the end
  flagcxRegItem reg{beginAddr, endAddr, 1, {}};
  regCommPool.push_back(std::move(reg));
  regMap[commKey][reinterpret_cast<uintptr_t>(data)] = &regCommPool.back();
  return flagcxSuccess;
}

flagcxResult_t flagcxRegPool::deregisterBuffer(void *comm, void *handle) {
  if (comm == nullptr || handle == nullptr) {
    return flagcxSuccess;
  }

  uintptr_t commKey = reinterpret_cast<uintptr_t>(comm);
  flagcxRegItem *reg = (flagcxRegItem *)handle;

  auto &regCommPool = regPool[commKey];
  for (auto it = regCommPool.begin(); it != regCommPool.end(); it++) {
    if (&(*it) == reg) {
      it->refCount--;
      if (it->refCount > 0) {
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
      return flagcxSuccess;
    }
  }

  WARN("Could not find the given handle in regPool");
  return flagcxInvalidUsage;
}

std::map<uintptr_t, std::map<uintptr_t, flagcxRegItem *>> &
flagcxRegPool::getGlobalMap() {
  return regMap;
}

flagcxRegItem *flagcxRegPool::getItem(const void *comm, void *data) {
  uintptr_t commKey = reinterpret_cast<uintptr_t>(comm);
  uintptr_t beginAddr, endAddr;
  getPagedAddr(data, 0, &beginAddr, &endAddr);
  auto it = regMap[commKey].find(beginAddr);
  if (it == regMap[commKey].end()) {
    return nullptr;
  }
  return it->second;
}

void flagcxRegPool::dump() {
  printf("========================\n");
  printf("RegPool(pageSize=%lu\n", pageSize);
  for (auto &c : regMap) {
    printf("==comm(%lu)==\n", c.first);
    for (auto &p : c.second) {
      printf("beginAddr(%lu) -> regItem[%lu,%lu,%d]\n", p.first,
             p.second->beginAddr, p.second->endAddr, p.second->refCount);
      auto it = p.second->netHandles.begin();
      for (; it != p.second->netHandles.end(); it++) {
        printf("handlePtr(%p) -> netHandle[%p,%p]\n", &(*it), it->handle,
               it->proxyConn);
      }
    }
    printf("==comm(%lu)==\n", c.first);
  }
  printf("========================\n");
}