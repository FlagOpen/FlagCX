from utils import *
from semantics import *
import xml.etree.ElementTree as ET
import xml.dom.minidom


class Stage(Enum):
    PreHomoFunc = 0
    HeteroFunc = 1
    HomoInterFunc = 2
    PostHomoFunc = 3


class Param(Enum):
    rank = 0
    peer_or_root_rank = 1
    send_type = 2
    recv_type = 3
    send_offset = 4
    recv_offset = 5
    count = 6
    homo_type = 7
    comm_op = 8
    send_buff = 9
    recv_buff = 10
    sub_step = 11


class FlagcxC2cPlanner:
    class RefreshFunc:
        def __init__(self, buff_type_: int, start_: int,  offset_: int, count_: int, total_count_: int, red_op_: RedOp):
            self.buffType = buff_type_
            self.start = start_
            self.offset = offset_
            self.count = count_
            self.totalCount = total_count_
            self.redOp = red_op_.value

        def to_xml(self):
            elem = ET.Element("RefreshFunc")
            for tag, val in vars(self).items():
                ET.SubElement(elem, tag).text = str(val)
            return elem

    class HomoFunc:
        def __init__(self,
                     root_rank_: int = -1,
                     send_type_: int = 0,
                     recv_type_: int = 1,
                     send_offset_: int = 0,
                     recv_offset_: int = 0,
                     count_: int = 0,
                     homo_type_: int = 0,
                     comm_op_: Primitive = Primitive.Nop,
                     params_: Optional[Dict] = None):
            self.rootRank: int = root_rank_
            self.sendType: int = send_type_
            self.recvType: int = recv_type_
            self.sendOffset: int = send_offset_
            self.recvOffset: int = recv_offset_
            self.count: int = count_
            self.homoType: int = homo_type_
            self.commOp: Primitive = comm_op_.value
            if params_ is not None:
                self.rootRank = params_.get(Param.peer_or_root_rank, -1)
                if Param.send_buff in params_.keys():
                    send_type_ = params_[Param.send_buff].buffer().type
                    send_offset_ = params_[Param.send_buff].offset
                if Param.recv_buff in params_.keys():
                    recv_type_ = params_[Param.recv_buff].buffer().type
                    recv_offset_ = params_[Param.recv_buff].offset
                self.sendType = params_.get(Param.send_type, send_type_)
                self.recvType = params_.get(Param.recv_type, recv_type_)
                self.sendOffset = params_.get(Param.send_offset, send_offset_)
                self.recvOffset = params_.get(Param.recv_offset, recv_offset_)
                self.count = params_.get(Param.count, 0)
                self.homoType = params_.get(Param.homo_type, 0)
                self.commOp = params_.get(Param.comm_op, Primitive.Nop).value

        def to_xml(self):
            elem = ET.Element("HomoFunc")
            for tag, val in vars(self).items():
                ET.SubElement(elem, tag).text = str(val)
            return elem

    class HeteroFunc:
        class P2pOp:
            def __init__(self, rank_: int, peer_rank_: int, offset_: int, count_: int, is_recv_: int):
                self.rank = rank_
                self.peerRank = peer_rank_
                self.offset = offset_
                self.count = count_
                self.isRecv = is_recv_

            def to_xml(self):
                elem = ET.Element("P2pOp")
                for tag, val in vars(self).items():
                    ET.SubElement(elem, tag).text = str(val)
                return elem

        def __init__(self):
            self.p2pOps: List[FlagcxC2cPlanner.HeteroFunc.P2pOp] = []

        def to_xml(self):
            elem = ET.Element("HeteroFunc")
            for op in self.p2pOps:
                elem.append(op.to_xml())
            return elem

    class Step:
        def __init__(self, homo_funcs=None, hetero_funcs=None):
            self.homo_funcs: List[FlagcxC2cPlanner.HomoFunc] = homo_funcs
            self.hetero_funcs: List[FlagcxC2cPlanner.HeteroFunc] = hetero_funcs

        def to_xml(self):
            step = ET.Element("Step")
            for homo_func in self.homo_funcs:
                step.append(homo_func.to_xml())
            for hetero_func in self.hetero_funcs:
                step.append(hetero_func.to_xml())
            return step

    def __init__(self, rank_: int,
                 n_seq_pre: int, n_pipe_pre: int, n_seq_inter: int, n_pipe_post: int, n_seq_post: int):
        self.rank = rank_
        self.nSeqPreSteps = n_seq_pre
        self.nPipePreSteps = n_pipe_pre
        self.nSeqInterSteps = n_seq_inter
        self.nPipePostSteps = n_pipe_post
        self.nSeqPostSteps = n_seq_post
        self.refresh_func = None
        self.pre_homo_steps: List[List[FlagcxC2cPlanner.HomoFunc]] = []
        self.hetero_steps: List[List[FlagcxC2cPlanner.HeteroFunc]] = []
        self.homo_inter_steps: List[List[FlagcxC2cPlanner.HomoFunc]] = []
        self.post_homo_steps: List[List[FlagcxC2cPlanner.HomoFunc]] = []

    def add_p2p_op(self, hetero_func: HeteroFunc,
                   rank_: int = -1,
                   peer_rank_: int = -1,
                   offset_: int = -1,
                   count_: int = -1,
                   is_recv_: int = -1,
                   params_: Optional[Dict] = None):
        if params_ is None:
            hetero_func.p2pOps.append(hetero_func.P2pOp(rank_, peer_rank_, offset_, count_, is_recv_))
        else:
            rank_ = self.rank
            peer_rank_ = params_[Param.peer_or_root_rank]
            is_recv_ = 0
            offset_ = params_[Param.send_offset]
            if params_[Param.send_offset] < 0:
                is_recv_ = 1
                offset_ = params_[Param.recv_offset]
            count_ = params_[Param.count]
            hetero_func.p2pOps.append(hetero_func.P2pOp(rank_, peer_rank_, offset_, count_, is_recv_))

    def set_refresh(self, buff_type_: int, start_: int, offset_: int, count_: int, total_count_: int, red_op_: RedOp):
        self.refresh_func = FlagcxC2cPlanner.RefreshFunc(buff_type_, start_, offset_, count_, total_count_, red_op_)

    def to_xml(self):
        root = ET.Element("FlagcxC2cPlanner")
        for tag in ["nSeqPreSteps", "nPipePreSteps", "nSeqInterSteps", "nPipePostSteps", "nSeqPostSteps"]:
            ET.SubElement(root, tag).text = str(getattr(self, tag))

        if self.refresh_func:
            root.append(self.refresh_func.to_xml())

        def add_steps(tag_name, steps):
            d1 = ET.SubElement(root, tag_name)
            for step in steps:
                d2 = ET.SubElement(d1, "Step")
                for func in step:
                    d2.append(func.to_xml())

        add_steps("PreHomoFuncSteps", self.pre_homo_steps)
        add_steps("HeteroFuncSteps", self.hetero_steps)
        add_steps("HomoInterFuncSteps", self.homo_inter_steps)
        add_steps("PostHomoFuncSteps", self.post_homo_steps)

        return root


def _curr():
    global _block
    if _block is None:
        raise RuntimeError("Not in a FlagCXBlock context")
    return _block


def _buff():
    return _curr().buffer


class IrStep:
    def __init__(self, step: int, prim: Primitive, params: Dict):
        self.step = step
        self.prim: Primitive = prim
        self.params: Dict = params


DataRef: TypeAlias = Tuple[RedOp, List[int]]


class Buffer:
    def __init__(self, id_: int, rank_: int, type_: int, size_: int):
        self.id = id_
        self.rank = rank_
        self.type = type_
        self.size = size_
        self.data: List[DataRef] = []

    def get_data(self, index_):
        if index_ >= len(self.data):
            raise RuntimeError("Invalid DataRef index")
        return self.data[index_]


class BuffRef:
    def __init__(self, bid_: int = -1, offset_: int = -1, count_: int = -1):
        self.bid = bid_
        self.offset = offset_
        self.count = count_

    def buffer(self):
        return _buff()[self.bid]

    def is_valid(self):
        return 0 <= self.bid < len(_buff()) and self.offset >= 0 and self.count >= 0


class FlagcxWorkflow:
    def __init__(self, name: str, coll: Collective, pre_data: DataCond, post_data: DataCond, world_size: int):
        self.name = name
        self.coll = coll
        self.pre_data = pre_data
        self.post_data = post_data
        self.world_size = world_size
        self.planner: List[FlagcxC2cPlanner] = []
        # for future extensibility
        self.buffer: List[Buffer] = []
        self.ir_steps: List[List[IrStep]] = []
        self._bid = count(start=0, step=1)

    def __enter__(self):
        global _block
        if _block is not None:
            raise RuntimeError("Already in a FlagCXBlock context")
        _block = self
        pass

    def __exit__(self, exc_type, exc_val, exc_tb):
        global _block
        if _block is None:
            raise RuntimeError("Not in a FlagCXBlock context")
        _block = None
        pass

    def add_buffer(self, rank_, type_, size_):
        bid = next(self._bid)
        self.buffer.append(Buffer(bid, rank_, type_, size_))
        return bid

    def to_xml(self, rank: int, file: Optional[str] = None) -> str:
        elem = self.planner[rank].to_xml()
        xml_string = ET.tostring(elem, encoding="utf-8")
        parsed_string = xml.dom.minidom.parseString(xml_string)
        pretty_string = parsed_string.toprettyxml(indent="  ")
        pretty_string = "\n".join(line for line in pretty_string.splitlines() if not line.strip().startswith("<?xml"))
        if file is not None:
            with open(file, "w", encoding="utf-8") as f:
                f.write(pretty_string)
        return pretty_string

    def semantic_check(self) -> bool:
        if self.coll != Collective.Custom:
            return True
        else:
            return False


_block: Optional[FlagcxWorkflow] = None


# DSL primitives
def set_pipeline(n_seq_pre_: int, n_pipe_pre_: int, n_seq_inter_: int, n_pipe_post_: int, n_seq_post_: int):
    for r in range(_curr().world_size):
        _curr().planner.append(FlagcxC2cPlanner(r, n_seq_pre_, n_pipe_pre_, n_seq_inter_, n_pipe_post_, n_seq_post_))


def set_refresh(rank_: int, buff_type_: int, start_: int, offset_: int, count_: int, total_count_: int, red_op_: RedOp):
    _curr().planner[rank_].set_refresh(buff_type_, start_, offset_, count_, total_count_, red_op_)


def add_opr(rank_: int, stage_: Stage, step_: int, params_: Optional[Dict]):
    if stage_ == Stage.PreHomoFunc:
        while len(_curr().planner[rank_].pre_homo_steps) <= step_:
            _curr().planner[rank_].pre_homo_steps.append([])
        if params_ is not None:
            _curr().planner[rank_].pre_homo_steps[step_].append(FlagcxC2cPlanner.HomoFunc(params_=params_))
    elif stage_ == Stage.PostHomoFunc:
        while len(_curr().planner[rank_].post_homo_steps) <= step_:
            _curr().planner[rank_].post_homo_steps.append([])
        if params_ is not None:
            _curr().planner[rank_].post_homo_steps[step_].append(FlagcxC2cPlanner.HomoFunc(params_=params_))
    elif stage_ == Stage.HomoInterFunc:
        while len(_curr().planner[rank_].homo_inter_steps) <= step_:
            _curr().planner[rank_].homo_inter_steps.append([])
        if params_ is not None:
            _curr().planner[rank_].homo_inter_steps[step_].append(FlagcxC2cPlanner.HomoFunc(params_=params_))
    elif stage_ == Stage.HeteroFunc:
        while len(_curr().planner[rank_].hetero_steps) <= step_:
            _curr().planner[rank_].hetero_steps.append([])
        if params_ is not None:
            if len(_curr().planner[rank_].hetero_steps[step_]) <= params_.get(Param.sub_step, 0):
                _curr().planner[rank_].hetero_steps[step_].append(FlagcxC2cPlanner.HeteroFunc())
            hetero_func = _curr().planner[rank_].hetero_steps[step_][params_.get(Param.sub_step, 0)]
            _curr().planner[rank_].add_p2p_op(hetero_func, params_=params_)
    else:
        raise RuntimeError("Instruction not supported")


def init_buffer(rank_: int, type_: int, size_: int):
    return _curr().add_buffer(rank_, type_, size_)


def get_planner(rank_: int):
    if rank_ >= len(_curr().planner):
        raise RuntimeError(f"No planner for rank {rank_}")
    return _curr().planner[rank_]


def to_xml(rank_: int = 0, path_: str = "") -> str:
    file_ = f"{path_}/{_curr().name}_{rank_}.xml"
    return _curr().to_xml(rank=rank_, file=file_)


def check() -> bool:
    return _curr().semantic_check()
