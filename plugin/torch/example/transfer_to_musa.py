import torch

def _redirect_cuda_to_musa():
    """Monkey-patch torch.cuda to behave like torch.musa."""
    if not hasattr(torch, "musa"):
        raise RuntimeError("torch.musa is not available in this environment")

    # Patch common functions
    torch.cuda.is_available = torch.musa.is_available
    torch.cuda.set_device = torch.musa.set_device

    # Patch Tensor.cuda() -> Tensor.to("musa")
    def _tensor_cuda(self, device=None, non_blocking=False):
        return self.to("musa", non_blocking=non_blocking)

    torch.Tensor.cuda = _tensor_cuda

# Apply the patch immediately on import
_redirect_cuda_to_musa()
