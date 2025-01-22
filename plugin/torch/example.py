import os

import torch
try:
    #deps for cambricon devices
    import torch_mlu
    from torch_mlu.utils.gpu_migration import migration
    dev_name = "mlu"
except:
    dev_name = "cuda"

import flagcx
import torch.distributed as dist
#import pdb
#pdb.set_trace()

# Get rank and world_size from environment
rank = int(os.environ["RANK"])
world_size = int(os.environ["WORLD_SIZE"])
prev_rank = (rank - 1 + world_size) % world_size
next_rank = (rank + 1) % world_size
print(f"rank: {rank}, world_size = {world_size}, prev_rank: {prev_rank}, next_rank: {next_rank}")

# Initialize the flagcx process group
dist.init_process_group(f"cpu:gloo,{dev_name}:flagcx", rank=rank, world_size=world_size)
print(f"ddp backend config is {dist.get_backend_config()}")

# Create two groups
ranks = list(range(world_size))
FLAGCX_GROUP1 = dist.new_group(ranks=ranks, backend=f"cpu:gloo,{dev_name}:flagcx")
FLAGCX_GROUP2 = dist.new_group(ranks=ranks, backend=f"cpu:gloo,{dev_name}:flagcx")
ranks_flagcx = dist.get_process_group_ranks(FLAGCX_GROUP1)
print(f"ranks_flagcx: {ranks_flagcx}")

if torch.cuda.is_available():
    # Create tensors
    torch.cuda.set_device(rank)
    x = torch.rand(world_size).cuda()
    y = torch.rand(world_size).cuda()
    print(f"rank {rank} initial: x = {x}, y = {y}")

    # Perform allreduce with FLAGCX_GROUP1
    dist.all_reduce(x, op=dist.ReduceOp.MIN, group=FLAGCX_GROUP1)
    print(f"rank {rank} after allreduce min with FLAGCX_GROUP1: x = {x}")
    dist.all_reduce(y, op=dist.ReduceOp.MAX, group=FLAGCX_GROUP1)
    print(f"rank {rank} after allreduce max with FLAGCX_GROUP1: y = {y}")
    dist.all_reduce(x, op=dist.ReduceOp.SUM, group=FLAGCX_GROUP1)
    print(f"rank {rank} after allreduce sum with FLAGCX_GROUP1: x = {x}")
    dist.barrier(group=FLAGCX_GROUP1)

    # Perform send and recv with FLAGCX_GROUP2
    for i in range(world_size):
        x[i] = rank
    print(f"rank {rank} before batch_isend_irecv with FLAGCX_GROUP2: x = {x}, y = {y}")
    op_send = dist.P2POp(dist.isend, x, next_rank, group=FLAGCX_GROUP2)
    op_recv = dist.P2POp(dist.irecv, y, prev_rank, group=FLAGCX_GROUP2)
    op_list = [op_send, op_recv]
    if rank % 2 == 1:
        op_list.reverse()
    reqs = dist.batch_isend_irecv(op_list)
    for req in reqs:
        req.wait()
    print(f"rank {rank} after batch_isend_irecv with FLAGCX_GROUP2: x = {x}, y = {y}")
    if rank % 2 == 0:
        dist.send(x, next_rank, group=FLAGCX_GROUP2)
        dist.send(x, next_rank, group=FLAGCX_GROUP2)
    elif rank % 2 == 1:
        dist.recv(y, prev_rank, group=FLAGCX_GROUP2)
        dist.recv(y, prev_rank, group=FLAGCX_GROUP2)
    print(f"rank {rank} after send/recv with FLAGCX_GROUP2: x = {x}, y = {y}")
    handle = dist.barrier(group=FLAGCX_GROUP2, async_op=True)
    handle.wait()

    # Perform allgather with FLAGCX_GROUP1
    z = torch.rand(1).cuda()
    z[0] = rank
    dist._all_gather_base(y, z, group=FLAGCX_GROUP1)
    print(f"rank {rank} after _all_gather_base with FLAGCX_GROUP1: z = {z}, y = {y}")
    z_list = list(torch.chunk(x, world_size, dim=0))
    dist.all_gather(z_list, z, group=FLAGCX_GROUP1)
    print(f"rank {rank} after all_gather with FLAGCX_GROUP1: z = {z}, z_list = {z_list}")
    print(z_list)
    all_rank_infos = [None] * world_size
    cur_rank_info = {'rank': rank, 'device_type': f"cpu:gloo,{dev_name}:flagcx"}
    dist.all_gather_object(all_rank_infos, cur_rank_info)
    print(f"rank {rank} after all_gather_object with FLAGCX_GROUP1: all_rank_infos = {all_rank_infos}")
    dist.barrier(group=FLAGCX_GROUP1)

    # Perform broadcast with FLAGCX_GROUP2
    x = torch.rand(world_size).cuda()
    print(f"rank {rank} before broadcast with FLAGCX_GROUP2: x = {x}")
    dist.broadcast(x, 0, group=FLAGCX_GROUP2)
    print(f"rank {rank} after broadcast with FLAGCX_GROUP2: x = {x}")
    dist.barrier(group=FLAGCX_GROUP2)

    # Perform reducescatter with FLAGCX_GROUP1
    z[0] = 0
    for i in range(world_size):
        x[i] = i
    x_list = list(torch.chunk(x, world_size, dim=0))
    print(f"rank {rank} before reduce_scatter with FLAGCX_GROUP1: x_list = {x_list}, z = {z}")
    dist.reduce_scatter(z, x_list, op=dist.ReduceOp.SUM, group=FLAGCX_GROUP1)
    print(f"rank {rank} after reduce_scatter with FLAGCX_GROUP1: x_list = {x_list}, z = {z}")
    dist.barrier(group=FLAGCX_GROUP1)
    for i in range(world_size):
        x[i] = rank
    print(f"rank {rank} before _reduce_scatter_base with FLAGCX_GROUP1: x = {x}, z = {z}")
    dist._reduce_scatter_base(z, x, op=dist.ReduceOp.MAX, group=FLAGCX_GROUP1)
    print(f"rank {rank} after _reduce_scatter_base with FLAGCX_GROUP1: x = {x}, z = {z}")

    # Perform alltoall with FLAGCX_GROUP2
    for i in range(world_size):
        y[i] = rank
        x[i] = 0
    list_y = list(torch.chunk(y, world_size, dim=0))
    list_z = list(torch.chunk(x, world_size, dim=0))
    print(f"rank {rank} before all_to_all with FLAGCX_GROUP2: list_y = {list_y}, list_z = {list_z}")
    dist.all_to_all(list_z, list_y, group=FLAGCX_GROUP2)
    print(f"rank {rank} after all_to_all with FLAGCX_GROUP2: list_y = {list_y}, list_z = {list_z}")
    dist.barrier(group=FLAGCX_GROUP2)

dist.destroy_process_group()