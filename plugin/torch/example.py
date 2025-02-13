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
FLAGCX_GROUP1 = dist.new_group(ranks=ranks, backend=f"{dev_name}:flagcx")
FLAGCX_GROUP2 = dist.new_group(ranks=ranks, backend=f"{dev_name}:flagcx")
ranks_flagcx = dist.get_process_group_ranks(FLAGCX_GROUP1)
print(f"ranks_flagcx: {ranks_flagcx}")

if torch.cuda.is_available():
    # Create tensors
    torch.cuda.set_device(rank % 8)
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

    # Perform all_reduce_coalesced sync with FLAGCX_GROUP1
    with dist._coalescing_manager(group=FLAGCX_GROUP1):
        dist.all_reduce(x, op=dist.ReduceOp.SUM, group=FLAGCX_GROUP1)
        dist.all_reduce(y, op=dist.ReduceOp.SUM, group=FLAGCX_GROUP1)
    print(f"rank {rank} after all_reduce_coalesced sync sum with FLAGCX_GROUP1: x = {x}, y = {y}")
    # Perform all_reduce_coalesced async with FLAGCX_GROUP1
    with dist._coalescing_manager(group=FLAGCX_GROUP1, async_ops=True)  as cm:
        dist.all_reduce(x,op=dist.ReduceOp.SUM, group=FLAGCX_GROUP1)
        dist.all_reduce(y,op=dist.ReduceOp.SUM, group=FLAGCX_GROUP1)
    cm.wait()
    print(f"rank {rank} after all_reduce_coalesced async sum with FLAGCX_GROUP1: x = {x}, y = {y}")

    # Perform send and recv with FLAGCX_GROUP2
    for i in range(world_size):
        x[i] = rank
    print(f"rank {rank} before batch_isend_irecv with FLAGCX_GROUP2: x = {x}, y = {y}")
    op_send = dist.P2POp(dist.isend, x, next_rank, group=FLAGCX_GROUP2)
    op_recv = dist.P2POp(dist.irecv, y, prev_rank, group=FLAGCX_GROUP2)
    op_list = [op_send, op_recv]
    reqs = dist.batch_isend_irecv(op_list)
    for req in reqs:
        req.wait()
    print(f"rank {rank} after batch_isend_irecv with FLAGCX_GROUP2: x = {x}, y = {y}")
    if rank % 2 == 0:
        dist.send(x, next_rank, group=FLAGCX_GROUP2)
        dist.recv(y, prev_rank, group=FLAGCX_GROUP2)
    elif rank % 2 == 1:
        dist.recv(y, prev_rank, group=FLAGCX_GROUP2)
        dist.send(x, next_rank, group=FLAGCX_GROUP2)
    handle = dist.barrier(group=FLAGCX_GROUP2, async_op=True)
    handle.wait()
    print(f"rank {rank} after send/recv with FLAGCX_GROUP2: x = {x}, y = {y}")

    # Perform allgather with FLAGCX_GROUP1
    z = torch.rand(1).cuda()
    z[0] = rank
    print(f"rank {rank} before _all_gather_base with FLAGCX_GROUP1: z = {z}, y = {y}")
    dist._all_gather_base(y, z, group=FLAGCX_GROUP1)
    print(f"rank {rank} after _all_gather_base with FLAGCX_GROUP1: z = {z}, y = {y}")
    z_list = list(torch.chunk(x, world_size, dim=0))
    print(f"rank {rank} before all_gather with FLAGCX_GROUP1: z = {z}, z_list = {z_list}")
    dist.all_gather(z_list, z, group=FLAGCX_GROUP1)
    print(f"rank {rank} after all_gather with FLAGCX_GROUP1: z = {z}, z_list = {z_list}")
    all_rank_infos = [None] * world_size
    cur_rank_info = {'rank': rank, 'device_type': f"cpu:gloo,{dev_name}:flagcx"}
    dist.all_gather_object(all_rank_infos, cur_rank_info)
    print(f"rank {rank} after all_gather_object with FLAGCX_GROUP1: all_rank_infos = {all_rank_infos}")

    # Perform all_gather_coalesced sync with FLAGCX_GROUP1
    z1 = torch.rand(1).cuda()
    z1[0] = rank * 10
    with dist._coalescing_manager(group=FLAGCX_GROUP1):
        dist.all_gather_into_tensor(x, z1, group=FLAGCX_GROUP1)
        dist.all_gather_into_tensor(y, z, group=FLAGCX_GROUP1)
    print(f"rank {rank} after all_gather_coalesced sync with FLAGCX_GROUP1: x = {x}, y = {y}")
    x = torch.rand(world_size).cuda()
    y = torch.rand(world_size).cuda()
    # Perform all_gather_coalesced async with FLAGCX_GROUP1
    with dist._coalescing_manager(group=FLAGCX_GROUP1, async_ops=True)  as cm:
        dist.all_gather_into_tensor(x, z1, group=FLAGCX_GROUP1)
        dist.all_gather_into_tensor(y, z, group=FLAGCX_GROUP1)
    cm.wait()
    print(f"rank {rank} after all_gather_coalesced async with FLAGCX_GROUP1: x = {x}, y = {y}")

    # Perform alltoall with FLAGCX_GROUP2
    for i in range(world_size):
        y[i] = rank
        x[i] = 0
    list_y = list(torch.chunk(y, world_size, dim=0))
    list_z = list(torch.chunk(x, world_size, dim=0))
    print(f"rank {rank} before all_to_all with FLAGCX_GROUP2: list_y = {list_y}, list_z = {list_z}")
    dist.all_to_all(list_z, list_y, group=FLAGCX_GROUP2)
    print(f"rank {rank} after all_to_all with FLAGCX_GROUP2: list_y = {list_y}, list_z = {list_z}")

    # Perform reducescatter with FLAGCX_GROUP1
    z[0] = 0
    for i in range(world_size):
        x[i] = i
    x_list = list(torch.chunk(x, world_size, dim=0))
    print(f"rank {rank} before reduce_scatter with FLAGCX_GROUP1: x_list = {x_list}, z = {z}")
    dist.reduce_scatter(z, x_list, op=dist.ReduceOp.SUM, group=FLAGCX_GROUP1)
    print(f"rank {rank} after reduce_scatter with FLAGCX_GROUP1: x_list = {x_list}, z = {z}")
    for i in range(world_size):
        x[i] = rank
    print(f"rank {rank} before _reduce_scatter_base with FLAGCX_GROUP1: x = {x}, z = {z}")
    dist._reduce_scatter_base(z, x, op=dist.ReduceOp.MAX, group=FLAGCX_GROUP1)
    print(f"rank {rank} after _reduce_scatter_base with FLAGCX_GROUP1: x = {x}, z = {z}")

    # Perform reduce_scatter_coalesced sync with FLAGCX_GROUP1
    x = torch.rand(world_size).cuda()
    y = torch.rand(world_size).cuda()
    z[0] = 0
    z1[0] = 0
    with dist._coalescing_manager(group=FLAGCX_GROUP1):
        dist.reduce_scatter_tensor(z, x, op=dist.ReduceOp.SUM, group=FLAGCX_GROUP1)
        dist.reduce_scatter_tensor(z1, y, op=dist.ReduceOp.SUM, group=FLAGCX_GROUP1)
    print(f"rank {rank} after reduce_scatter_coalesced sync with FLAGCX_GROUP1: x = {x}, y = {y}, z={z}, z1={z1}")
    z[0] = 0
    z1[0] = 0
    # Perform reduce_scatter_coalesced async with FLAGCX_GROUP1
    with dist._coalescing_manager(group=FLAGCX_GROUP1, async_ops=True)  as cm:
        dist.reduce_scatter_tensor(z, x, op=dist.ReduceOp.SUM, group=FLAGCX_GROUP1)
        dist.reduce_scatter_tensor(z1, y, op=dist.ReduceOp.SUM, group=FLAGCX_GROUP1)
    cm.wait()
    print(f"rank {rank} after reduce_scatter_coalesced async with FLAGCX_GROUP1: x = {x}, y = {y}, z={z}, z1={z1}")

    # Perform broadcast with FLAGCX_GROUP2
    x = torch.rand(world_size).cuda()
    print(f"rank {rank} before broadcast with FLAGCX_GROUP2: x = {x}")
    dist.broadcast(x, 0, group=FLAGCX_GROUP2)
    print(f"rank {rank} after broadcast with FLAGCX_GROUP2: x = {x}")

    # Perform gather with FLAGCX_GROUP1
    z[0] = rank
    x = torch.rand(world_size).cuda()
    x_list = list(torch.chunk(x, world_size, dim=0))
    print(f"rank {rank} before gather on dst rank 0 with FLAGCX_GROUP1: z = {z}, x_list = {x_list}")
    if rank == 0:
        handle = dist.gather(z, x_list, 0, group=FLAGCX_GROUP1, async_op=True)
    else:
        handle = dist.gather(z, None, 0, group=FLAGCX_GROUP1, async_op=True)
    handle.wait()
    print(f"rank {rank} after gather on dst rank 0 with FLAGCX_GROUP1: z = {z}, x_list = {x_list}")

    # Perform scatter with FLAGCX_GROUP2
    z[0] = -1
    print(f"rank {rank} before scatter from src rank 0 with FLAGCX_GROUP2: z = {z}, x_list = {x_list}")
    if rank == 0:
        dist.scatter(z, x_list, 0, group=FLAGCX_GROUP2)
    else:
        dist.scatter(z, None, 0, group=FLAGCX_GROUP2)
    print(f"rank {rank} after scatter from src rank 0 with FLAGCX_GROUP2: z = {z}, x_list = {x_list}")

    # Perform reduce with FLAGCX_GROUP1
    for i in range(world_size):
        x[i] = i
    print(f"rank {rank} before reduce on src rank 0 with FLAGCX_GROUP1: x = {x}")
    dist.reduce(x, 0, op=dist.ReduceOp.SUM, group=FLAGCX_GROUP1)
    print(f"rank {rank} after reduce on src rank 0 with FLAGCX_GROUP1: x = {x}")

dist.destroy_process_group()
