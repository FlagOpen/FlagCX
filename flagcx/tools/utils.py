from typing import *
from enum import *
from itertools import count


class Collective(Enum):
    P2P = 0
    Custom = 1
    Broadcast = 2
    Gather = 3
    Scatter = 4
    Reduce = 5
    AllReduce = 6
    AllGather = 7
    ReduceScatter = 8
    AlltoAll = 9
    AlltoAllv = 10
    Nop = 11


class RedOp(Enum):
    sum = 0
    prod = 1
    max = 2
    min = 3
    avg = 4
    nop = 5


class Primitive(Enum):
    P2P = 0
    Custom = 1
    Broadcast = 2
    Gather = 3
    Scatter = 4
    Reduce = 5
    AllReduce = 6
    AllGather = 7
    ReduceScatter = 8
    AlltoAll = 9
    AlltoAllv = 10
    Nop = 11
    LocCpy = 12
    LocRed = 13
