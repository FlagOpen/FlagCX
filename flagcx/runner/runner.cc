/*************************************************************************
 * Copyright (c) 2025 BAAI. All rights reserved.
 ************************************************************************/

#include "runner.h"

struct flagcxRunner *flagcxRunners[NRUNNERS] = {&homoRunner, NULL, NULL, NULL};
//  &hostRunner,
//  &hybridRunner,
//  &uniRunner};