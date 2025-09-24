#include "reg_pool.h"
#include <cstdio>

flagcxRegPool::flagcxRegPool() {
  pthread_mutex_init(&poolMutex, nullptr);
  pageSize = sysconf(_SC_PAGESIZE);
  // regPool.push_back({UINTPTR_MAX, UINTPTR_MAX, 1, 0, nullptr, nullptr});
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

void flagcxRegPool::registerBuffer(void *data, size_t length) {
  if (data == nullptr || length == 0)
    return;

  uintptr_t beginAddr, endAddr;
  getPagedAddr(data, length, &beginAddr, &endAddr);

  pthread_mutex_lock(&poolMutex);

  for (auto &reg : regPool) {
    if (beginAddr < reg.beginAddr) {
      regPool.push_front({beginAddr, endAddr, 1, 0, nullptr, nullptr});
      auto &newReg = regPool.front();
      regMap[reinterpret_cast<uintptr_t>(data)] = &newReg;
      pthread_mutex_unlock(&poolMutex);
      return;
    } else if ((reg.beginAddr <= beginAddr) && (reg.endAddr >= endAddr)) {
      reg.refCount++;
      regMap[reinterpret_cast<uintptr_t>(data)] = &reg;
      pthread_mutex_unlock(&poolMutex);
      return;
    }
  }

  regPool.push_back({beginAddr, endAddr, 1, 0, nullptr, nullptr});
  auto &newReg = regPool.back();
  regMap[reinterpret_cast<uintptr_t>(data)] = &newReg;
  pthread_mutex_unlock(&poolMutex);

  // auto it = regMap.lower_bound(beginAddr);
  // if (it != regMap.begin())
  //   --it;

  // std::vector<std::pair<uintptr_t, flagcxRegItem>> toAdd;
  // uintptr_t curBegin = beginAddr;
  // uintptr_t curEnd = endAddr;

  // while (it != regMap.end() && it->first < endAddr) {
  //   uintptr_t existBegin = it->first;
  //   uintptr_t existEnd = it->second.endAddr;
  //   // int existType = it->second.type;

  //   if (existEnd <= curBegin) {
  //     ++it;
  //     continue;
  //   }
  //   if (existBegin >= curEnd)
  //     break;

  //   if (existType == type) {
  //     curBegin = std::min(curBegin, existBegin);
  //     curEnd = std::max(curEnd, existEnd);
  //     it = regMap.erase(it);
  //   } else {
  //     if (curBegin < existBegin)
  //       toAdd.emplace_back(curBegin, flagcxRegItem{existBegin, type});
  //     curBegin = std::max(curBegin, existEnd);
  //     ++it;
  //   }
  // }

  // if (curBegin < curEnd)
  //   toAdd.emplace_back(curBegin, flagcxRegItem{curEnd, type});

  // for (auto &p : toAdd)
  //   regMap[p.first] = p.second;

  // dump();
}

void flagcxRegPool::deRegisterBuffer(void *data) {
  // if (data == nullptr || length == 0) return;

  // uintptr_t beginAddr, endAddr;
  // getPagedAddr(data, length, &beginAddr, &endAddr);

  pthread_mutex_lock(&poolMutex);

  auto it = regMap.find(reinterpret_cast<uintptr_t>(data));
  if (it != regMap.end()) {
    flagcxRegItem *reg = it->second;
    if (reg->refCount > 0) {
      reg->refCount--;
    }
  }

  // auto it = regMap.lower_bound(beginAddr);
  // if (it != regMap.begin())
  //   --it;

  // while (it != regMap.end() && it->first < endAddr) {
  //   uintptr_t existBegin = it->first;
  //   uintptr_t existEnd = it->second.endAddr;

  //   if (existEnd <= beginAddr) {
  //     ++it;
  //     continue;
  //   }
  //   if (existBegin >= endAddr)
  //     break;

  //   if (existBegin >= beginAddr && existEnd <= endAddr) {
  //     it = regMap.erase(it);
  //   } else if (existBegin < beginAddr && existEnd > endAddr) {
  //     flagcxRegItem info = it->second;
  //     info.endAddr = beginAddr;
  //     regMap[existBegin] = info;
  //     regMap[endAddr] = flagcxRegItem{existEnd, it->second.type,
  //     it->second.status, it->second.sendMrHandle, it->second.recvMrHandle};
  //     regMap.erase(it);
  //     break;
  //   } else if (existBegin < beginAddr) {
  //     it->second.endAddr = beginAddr;
  //     ++it;
  //   } else {
  //     flagcxRegItem info = it->second;
  //     regMap.erase(it);
  //     regMap[endAddr] = flagcxRegItem{existEnd, info.type, info.status,
  //     info.sendMrHandle, info.recvMrHandle}; break;
  //   }
  // }

  pthread_mutex_unlock(&poolMutex);
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

std::map<uintptr_t, flagcxRegItem *> &flagcxRegPool::getMap() { return regMap; }

flagcxRegItem *flagcxRegPool::getItem(uintptr_t key) {
  auto it = regMap.find(key);
  if (it == regMap.end()) {
    return nullptr;
  }
  return it->second;
}

void flagcxRegPool::dump() {
  pthread_mutex_lock(&poolMutex);
  printf("========================\n");
  for (auto &p : regMap)
    printf("%lu -> [%lu,%lu,%d,%d,%p,%p]\n", p.first, p.second->beginAddr,
           p.second->endAddr, p.second->refCount, p.second->status,
           p.second->sendMrHandle, p.second->recvMrHandle);
  printf("========================\n");
  pthread_mutex_unlock(&poolMutex);
}