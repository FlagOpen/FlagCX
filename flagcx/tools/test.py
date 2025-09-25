from algorithm import *

with FlagcxWorkflow("test", Collective.AllReduce, [], [], 8):
    # chunk_size = 8388608

    # init input and output buffer info
    rank_data = []
    for rank in range(8):
        rank_data.append([])
        for offset in range(8):
            send_id = init_buffer(rank, 0, 8)
            recv_id = init_buffer(rank, 1, 8)
            rank_data[rank].append(send_id)
            rank_data[rank].append(recv_id)

    # init pipeline schedule
    set_pipeline(1, 0, 1, 1, 1)
    # init refresh func info
    for rank in range(8):
        set_refresh(rank, 1, rank // 4 * 4, rank % 4, 1, 4, RedOp.sum)

    for rank in range(4):
        # pre homo funcs for cluster 0
        add_opr(rank_=rank,
                stage_=Stage.PreHomoFunc,
                step_=0,
                params_={Param.send_buff: BuffRef(rank_data[rank][0], 0, 4),
                         Param.recv_buff: BuffRef(rank_data[rank][1], rank, 1),
                         Param.count: 1,
                         Param.homo_type: 0,
                         Param.comm_op: Primitive.ReduceScatter})
        add_opr(rank_=rank,
                stage_=Stage.PreHomoFunc,
                step_=0,
                params_={Param.send_buff: BuffRef(rank_data[rank][0], 4, 4),
                         Param.recv_buff: BuffRef(rank_data[rank][1], rank + 4, 1),
                         Param.count: 1,
                         Param.homo_type: 0,
                         Param.comm_op: Primitive.ReduceScatter})
        # pre homo funcs for cluster 1
        add_opr(rank_=rank + 4,
                stage_=Stage.PreHomoFunc,
                step_=0,
                params_={Param.send_buff: BuffRef(rank_data[rank + 4][0], 0, 4),
                         Param.recv_buff: BuffRef(rank_data[rank + 4][1], rank - 4, 1),
                         Param.count: 1,
                         Param.homo_type: 0,
                         Param.comm_op: Primitive.ReduceScatter})
        add_opr(rank_=rank + 4,
                stage_=Stage.PreHomoFunc,
                step_=0,
                params_={Param.send_buff: BuffRef(rank_data[rank + 4][0], 4, 4),
                         Param.recv_buff: BuffRef(rank_data[rank + 4][1], rank, 1),
                         Param.count: 1,
                         Param.homo_type: 0,
                         Param.comm_op: Primitive.ReduceScatter})

    # hetero funcs
    for rank in range(4):
        # step 0
        add_opr(rank_=rank,
                stage_=Stage.HeteroFunc,
                step_=0,
                params_={Param.peer_or_root_rank: (rank + 1) % 4 + 4,
                         Param.send_offset: rank + 4,
                         Param.recv_offset: -1,
                         Param.count: 1})
        add_opr(rank_=rank,
                stage_=Stage.HeteroFunc,
                step_=0,
                params_={Param.peer_or_root_rank: (rank + 3) % 4 + 4,
                         Param.send_offset: -1,
                         Param.recv_offset: (rank + 3) % 4,
                         Param.count: 1})
        add_opr(rank_=rank + 4,
                stage_=Stage.HeteroFunc,
                step_=0,
                params_={Param.peer_or_root_rank: (rank + 1) % 4,
                         Param.send_offset: rank - 4,
                         Param.recv_offset: -1,
                         Param.count: 1})
        add_opr(rank_=rank + 4,
                stage_=Stage.HeteroFunc,
                step_=0,
                params_={Param.peer_or_root_rank: (rank + 3) % 4,
                         Param.send_offset: -1,
                         Param.recv_offset: (rank + 3) % 4 + 4,
                         Param.count: 1})
        # step 1
        add_opr(rank_=rank,
                stage_=Stage.HeteroFunc,
                step_=1,
                params_={Param.peer_or_root_rank: rank + 4,
                         Param.send_offset: rank,
                         Param.recv_offset: -1,
                         Param.count: 1})
        add_opr(rank_=rank,
                stage_=Stage.HeteroFunc,
                step_=1,
                params_={Param.peer_or_root_rank: rank + 4,
                         Param.send_offset: -1,
                         Param.recv_offset: rank + 4,
                         Param.count: 1})
        add_opr(rank_=rank + 4,
                stage_=Stage.HeteroFunc,
                step_=1,
                params_={Param.peer_or_root_rank: rank,
                         Param.send_offset: rank + 4,
                         Param.recv_offset: -1,
                         Param.count: 1})
        add_opr(rank_=rank + 4,
                stage_=Stage.HeteroFunc,
                step_=1,
                params_={Param.peer_or_root_rank: rank,
                         Param.send_offset: -1,
                         Param.recv_offset: rank,
                         Param.count: 1})

    # homo inter funcs
    for rank in range(4):
        # cluster 0
        add_opr(rank_=rank,
                stage_=Stage.HomoInterFunc,
                step_=0,
                params_={Param.send_buff: BuffRef(rank_data[rank][1], 0, 1),
                         Param.recv_buff: BuffRef(rank_data[rank][1], rank, 1),
                         Param.count: 1,
                         Param.homo_type: 2,
                         Param.comm_op: Primitive.ReduceScatter})
        add_opr(rank_=rank, stage_=Stage.HomoInterFunc, step_=1, params_=None)
        # cluster 1
        add_opr(rank_=rank + 4,
                stage_=Stage.HomoInterFunc,
                step_=0,
                params_={Param.send_buff: BuffRef(rank_data[rank + 4][1], 4, 1),
                         Param.recv_buff: BuffRef(rank_data[rank + 4][1], rank, 1),
                         Param.count: 1,
                         Param.homo_type: 2,
                         Param.comm_op: Primitive.ReduceScatter})
        add_opr(rank_=rank, stage_=Stage.HomoInterFunc, step_=1, params_=None)

    # post homo funcs
    for rank in range(4):
        for root in range(4):
            add_opr(rank_=rank,
                    stage_=Stage.PostHomoFunc,
                    step_=0,
                    params_={Param.peer_or_root_rank: root,
                             Param.send_buff: BuffRef(rank_data[rank][1], root, 1),
                             Param.recv_buff: BuffRef(rank_data[rank][1], root, 1),
                             Param.count: 1,
                             Param.homo_type: 2,
                             Param.comm_op: Primitive.Broadcast})
        for root in range(4):
            add_opr(rank_=rank,
                    stage_=Stage.PostHomoFunc,
                    step_=1,
                    params_={Param.peer_or_root_rank: root,
                             Param.send_buff: BuffRef(rank_data[rank][1], root + 4, 1),
                             Param.recv_buff: BuffRef(rank_data[rank][1], root + 4, 1),
                             Param.count: 1,
                             Param.homo_type: 2,
                             Param.comm_op: Primitive.Broadcast})
        for root in range(4):
            add_opr(rank_=rank + 4,
                    stage_=Stage.PostHomoFunc,
                    step_=0,
                    params_={Param.peer_or_root_rank: root + 4,
                             Param.send_buff: BuffRef(rank_data[rank + 4][1], root + 4, 1),
                             Param.recv_buff: BuffRef(rank_data[rank + 4][1], root + 4, 1),
                             Param.count: 1,
                             Param.homo_type: 2,
                             Param.comm_op: Primitive.Broadcast})
        for root in range(4):
            add_opr(rank_=rank + 4,
                    stage_=Stage.PostHomoFunc,
                    step_=1,
                    params_={Param.peer_or_root_rank: root + 4,
                             Param.send_buff: BuffRef(rank_data[rank + 4][1], root, 1),
                             Param.recv_buff: BuffRef(rank_data[rank + 4][1], root, 1),
                             Param.count: 1,
                             Param.homo_type: 2,
                             Param.comm_op: Primitive.Broadcast})

    # export as xml
    for rank in range(8):
        to_xml(rank_=rank, path_="output")
