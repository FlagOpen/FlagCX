from utils import *

"""
DataCond[rank][index] = (redop, datalist)
example: allreduce(sum, nranks)
pre_cond[rank][index] = (nop, [rank * nranks + index])
post_cond[rank][index] = (sum, [i * nranks + index for i in range(nranks)])
"""
DataCond: TypeAlias = List[List[Tuple[RedOp, List[int]]]]
