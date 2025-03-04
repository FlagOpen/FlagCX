# Disable auto load flagcx when load flagcx to
# avoid recursive init when only import flagcx without
# import torch
import os
os.environ["TORCH_DEVICE_BACKEND_AUTOLOAD"] = "0"
import torch
os.environ.pop('TORCH_DEVICE_BACKEND_AUTOLOAD')
from functools import wraps
import torch.distributed as dist

from ._C import *

def init():
    pass

def replace_prefix(arg):
    device_list = ["cuda", "mlu"]
    flagcx_prefix = "flagcx_dev"
    if isinstance(arg, str):
        for string in device_list:
            if string in arg:
                arg = arg.replace(string, flagcx_prefix)
    return arg

def replace_device_args(fn):
    @wraps(fn)
    def wrapper_fn(*args, **kwargs):
        if args:
            args = list(args)
            args[1]=replace_prefix(args[1])
        return fn(*args, **kwargs)
    return wrapper_fn

def replace_device(module, fn_name):
    fn = getattr(module, fn_name)
    if fn:
        setattr(module, fn_name, replace_device_args(fn))

replace_device(dist.distributed_c10d.PrefixStore, "__init__")

