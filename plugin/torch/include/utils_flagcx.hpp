#pragma once

#include "flagcx.h"

namespace c10d
{
  // RAII helper class to manage Flagcx group API.
  // The destructor is allowed to throw since this helper class only
  // manages group lifetimes.
  struct AutoFlagcxGroup final
  {
    AutoFlagcxGroup()
    {
      // TODO: support group semantics for heterogeneous case
      flagcxIsHomoComm(&is_homo_);
      if (is_homo_)
      {
        flagcxGroupStart();
      }
    }
    ~AutoFlagcxGroup() noexcept(false)
    {
      if (is_homo_)
      {
        flagcxGroupEnd();
      }
    }
    int is_homo_ = 1;
  };
} // namespace c10d