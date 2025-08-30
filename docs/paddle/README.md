# Training Models with Paddle and FlagCX

FlagCX is now fully integrated into Paddle as an **optional high-performance communication backend**. This integration enables efficient distributed training on multiple hardware platforms, including support for **heterogeneous training** on Nvidia and Iluvatar GPUs.  

Use the guides below to quickly get started with training models using Paddle + FlagCX.

---

## Homogeneous Training

Train on a single type of hardware platform:

| Hardware        | User Guide |
|:---------------:|:----------|
| Nvidia GPU      | [Get Started](nvidia.md) |
| Kunlunxin XPU   | [Get Started](kunlun.md) |
| Iluvatar GPU    | [Get Started](iluvatar.md) |

---

## Heterogeneous Training

Train across **different hardware platforms** simultaneously:

| Hardware Combination         | User Guide |
|:----------------------------:|:----------|
| Nvidia GPU + Iluvatar GPU    | [Get Started](nvidia_iluvatar_hetero_train.md) |

