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
      flagcxGroupStart();
    }
    ~AutoFlagcxGroup() noexcept(false)
    {
      flagcxGroupEnd();
    }
  };
} // namespace c10d