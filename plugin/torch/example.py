import os

import torch
try:
    #deps for cambricon devices
    import torch_mlu
    from torch_mlu.utils.gpu_migration import migration
except:
    pass

import flagcx
import torch.distributed as dist
#import pdb
#pdb.set_trace()

rank = int(os.environ["RANK"])
world_size = int(os.environ["WORLD_SIZE"])

dist.init_process_group("flagcx", rank=rank, world_size=world_size)
ranks = list(range(world_size))

FLAGCX_GROUP1 = dist.new_group(ranks=ranks, backend="flagcx")
FLAGCX_GROUP2 = dist.new_group(ranks=ranks, backend="flagcx")
ranks_flagcx = dist.get_process_group_ranks(FLAGCX_GROUP1)
print(f"ranks_flagcx: {ranks_flagcx}")

print(f"ddp backend config is {dist.get_backend_config()}")

x = torch.ones(world_size)
print(f"initial x: {x}")
# this goes through gloo
# dist.all_reduce(x)
# print(f"cpu allreduce x: {x}")

# this goes through flagcx
if torch.cuda.is_available():
    torch.cuda.set_device(rank)
    y = x.cuda()
    print(f"initial y: {y}")
    dist.all_reduce(y, group=FLAGCX_GROUP1)
    print(f"flagcx allreduce y with FLAGCX_GROUP1: {y}")
    dist.all_reduce(y, group=FLAGCX_GROUP2)
    print(f"flagcx allreduce y with FLAGCX_GROUP2: {y}")
    if rank == 0:
        for i in range(world_size):
            y[i] = rank
    print(f"medium y: {y}")
    list_y = list(torch.chunk(y, world_size, dim=0))
    print(list_y)
    list_z = list(torch.chunk(torch.ones(world_size).cuda(), world_size, dim=0))
    print(list_z)
    dist.all_to_all(list_z, list_y, group=FLAGCX_GROUP1)
    y = torch.cat(list_z, dim=0)
    print(f"flagcx alltoall y with FLAGCX_GROUP1: {y}")

dist.destroy_process_group()
