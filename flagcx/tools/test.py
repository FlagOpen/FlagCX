from algorithm import *

with FlagcxBlock("test", Collective.AllReduce, [], [], 8):
    chunk_size = 8388608

    # init input and output buffer info
    rank_data = []
    for rank in range(8):
        rank_data.append([])
        for offset in range(8):
            send_id = init_buffer(rank, 0, chunk_size * 8)
            recv_id = init_buffer(rank, 1, chunk_size * 8)
            rank_data[rank].append(send_id)
            rank_data[rank].append(recv_id)

    # init pipeline schedule
    set_pipeline(1, 0, 1, 1, 1)
    # init refresh func info
    set_refresh(1, 0, 0, chunk_size, chunk_size * 4, RedOp.sum)

    # pre homo funcs
    for rank in range(4):
        add_instr(rank_=rank,
                  stage_=Stage.PreHomoFunc,
                  step_=0,
                  params_={Param.send_buff: BuffRef(rank_data[rank][0], 0, chunk_size * 4),
                           Param.recv_buff: BuffRef(rank_data[rank][1], chunk_size * rank, chunk_size * 2),
                           Param.count: chunk_size,
                           Param.homo_type: 0,
                           Param.comm_op: Instr.ReduceScatter})
        add_instr(rank_=rank,
                  stage_=Stage.PreHomoFunc,
                  step_=0,
                  params_={Param.send_buff: BuffRef(rank_data[rank][0], chunk_size * 4, chunk_size * 4),
                           Param.recv_buff: BuffRef(rank_data[rank][1], chunk_size * (rank + 4), chunk_size),
                           Param.count: chunk_size,
                           Param.homo_type: 0,
                           Param.comm_op: Instr.ReduceScatter})

    # hetero funcs for rank 0
    # step 0
    add_instr(rank_=0,
              stage_=Stage.HeteroFunc,
              step_=0,
              params_={Param.peer_or_root_rank: 5,
                       Param.send_offset: chunk_size * 4,
                       Param.recv_offset: -1,
                       Param.count: chunk_size})
    add_instr(rank_=0,
              stage_=Stage.HeteroFunc,
              step_=0,
              params_={Param.peer_or_root_rank: 7,
                       Param.send_offset: -1,
                       Param.recv_offset: chunk_size * 3,
                       Param.count: chunk_size})
    # step 1
    add_instr(rank_=0,
              stage_=Stage.HeteroFunc,
              step_=1,
              params_={Param.peer_or_root_rank: 4,
                       Param.send_offset: 0,
                       Param.recv_offset: -1,
                       Param.count: chunk_size})
    add_instr(rank_=0,
              stage_=Stage.HeteroFunc,
              step_=1,
              params_={Param.peer_or_root_rank: 4,
                       Param.send_offset: -1,
                       Param.recv_offset: chunk_size * 4,
                       Param.count: chunk_size})

    # homo inter funcs
    add_instr(rank_=0,
              stage_=Stage.HomoInterFunc,
              step_=0,
              params_={Param.send_buff: BuffRef(rank_data[rank][1], 0, chunk_size),
                       Param.recv_buff: BuffRef(rank_data[rank][1], 0, chunk_size),
                       Param.count: chunk_size,
                       Param.homo_type: 2,
                       Param.comm_op: Instr.ReduceScatter})
    add_instr(rank_=0, stage_=Stage.HomoInterFunc, step_=1, params_=None)

    # post homo funcs
    for root in range(4):
        add_instr(rank_=0,
                  stage_=Stage.PostHomoFunc,
                  step_=0,
                  params_={Param.peer_or_root_rank: root,
                           Param.send_buff: BuffRef(rank_data[0][1], chunk_size * root, chunk_size),
                           Param.recv_buff: BuffRef(rank_data[0][1], chunk_size * root, chunk_size),
                           Param.count: chunk_size,
                           Param.homo_type: 2,
                           Param.comm_op: Instr.Broadcast})
    for root in range(4):
        add_instr(rank_=0,
                  stage_=Stage.PostHomoFunc,
                  step_=1,
                  params_={Param.peer_or_root_rank: root,
                           Param.send_buff: BuffRef(rank_data[0][1], chunk_size * (root + 4), chunk_size),
                           Param.recv_buff: BuffRef(rank_data[0][1], chunk_size * (root + 4), chunk_size),
                           Param.count: chunk_size,
                           Param.homo_type: 2,
                           Param.comm_op: Instr.Broadcast})

    # export as xml
    to_xml(rank_=0, path_="output")
