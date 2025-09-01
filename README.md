[<img src="flagopen.png">](https://flagopen.baai.ac.cn/)

## Latest News
- **[2025/07]** Released [v0.3](https://github.com/FlagOpen/FlagCX/tree/release/v0.3):
  - Integrates three additional native communication libraries: HCCL, MUSACCL and MPI.
  - Enhances heterogeneous collective communication operations with pipeline optimizations. 
  - Introduces a device-side function mechanism to enable device-buffer RDMA, complementing the original host-side function mechanism.
  - Delivers a full-stack open-source solution, FlagScale + FlagCX, for efficient heterogeneous prefilling-decoding disaggregation.
- **[2025/05]** Released [v0.2](https://github.com/FlagOpen/FlagCX/tree/release/v0.2):
  - Integrates three additional native communications libraries, including MCCL, XCCL and DUCCL.
  - Improves 11 heterogeneous collective communication operations with automatic topology detection, fully supporting both single-NIC and multi-NIC environments.
- **[2025/04]** Released [v0.1](https://github.com/FlagOpen/FlagCX/tree/release/v0.1):
  - Integrates five native communications libraries including NCCL, IXCCL, CNCL, BOOTSTRAP and GLOO.
  - Supports 11 heterogeneous collective communication operations using the originally proposed C2C (Cluster-to-Cluster) algorithm.
  - Provides a full-stack open-source solution, FlagScale + FlagCX, for efficient heterogeneous training.
  - Natively integrated into PaddlePaddle [v3.0.0](https://github.com/PaddlePaddle/Paddle/tree/v3.0.0), with support for both dynamic and static graphs.

## About

[FlagCX](https://github.com/FlagOpen/FlagCX.git) is a scalable and adaptive cross-chip communication library developed with the backing of the Beijing Academy of Artificial Intelligence (BAAI).

FlagCX is also a part of [FlagAI-Open](https://flagopen.baai.ac.cn/), an open-source initiative by BAAI that aims to foster an open-source ecosystem for AI technologies. It serves as a platform where developers, researchers, and AI enthusiasts can collaborate on various AI projects, contribute to the development of cutting-edge AI solutions, and share their work with the global community.

FlagCX leverages native collective communications libraries to provide the full support of single-chip communications on different platforms. In addition to its native x-CCL support, FlagCX provides an original device-buffer RDMA design to offer advanced support for cross-chip high-performance sendrecev operations, which can also be integrated with native x-CCL backends to enable optimized cross-chip collective communications. A comprehensive list of currently supported communication backends and their different capabilities are listed as follows:
| Backend       | NCCL        | IXCCL       | CNCL        | MCCL        | XCCL        | DUCCL       | HCCL        | MUSACCL     |
|:--------------|:------------|:------------|:------------|:------------|:------------|:------------|:------------|:------------|
| Mode          | Homo/Hetero | Homo/Hetero | Homo/Hetero | Homo/Hetero | Homo/Hetero | Homo/Hetero | Homo/Hetero | Homo/Hetero |
| send          | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/☓         | ✓/☓         | ✓/✓         |
| recv          | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/☓         | ✓/☓         | ✓/✓         |
| broadcast     | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/☓         | ✓/☓         | ✓/✓         |
| gather        | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ☓/☓         | ✓/☓         | ✓/☓         | ✓/✓         |
| scatter       | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/☓         | ✓/☓         | ✓/☓         | ✓/✓         |
| reduce        | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/☓         | ✓/☓         | ✓/✓         |
| allreduce     | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/☓         | ✓/☓         | ✓/✓         |
| allgather     | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/☓         | ✓/☓         | ✓/✓         |
| reducescatter | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/☓         | ✓/☓         | ✓/✓         |
| alltoall      | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/☓         | ✓/☓         | ✓/✓         |
| alltoallv     | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/☓         | ✓/☓         | ✓/☓         | ✓/✓         |
| group ops     | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/✓         | ✓/☓         | ✓/☓         | ✓/✓         |

Note that `Homo` and `Hetero` modes refer to communications among homogeneous and heterogeneous clusters. All native collective communications libraries can be referenced through the links below:

- [NCCL](https://github.com/NVIDIA/nccl), NVIDIA Collective Communications Library.
- [IXCCL](https://www.iluvatar.com/software?fullCode=cpjs-rj-rjz), Iluvatar Corex Collective Communications Library.
- [CNCL](https://www.cambricon.com/docs/sdk_1.7.0/cncl_1.2.1/user_guide/index.html#), Cambricon Communications Library.
- [MCCL](https://developer.metax-tech.com/softnova/metax), Metax Collective Communications Library.
- [XCCL](WIP), XPU Collective Communications Library.
- [DUCCL](https://developer.sourcefind.cn), DU Collective Communications Library.
- [HCCL](https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/82RC1alpha003/hccl/hcclug/hcclug_000001.html), Ascend Communications Library.
- [MUSACCL](https://docs.mthreads.com/musa-sdk/musa-sdk-doc-online/programming_guide/Chapter08/), Musa Collective Communications Library.

Additionally, FlagCX supports three collective communication libraries for host-side communication: `BOOTSTRAP`, `GLOO`, and `MPI`. Besides `BOOTSTRAP`, which is built using the FlagCX `bootstrap` component, the other two libraries are described as follows:

- [GLOO](https://github.com/facebookincubator/gloo), Gloo Collective Communications Library.
- [MPI](https://www.mpich.org), Message Passing Interface (MPI) standard.

FlagCX also integrates with upper-layer applications such as PyTorch and PaddlePaddle based on its unified APIs. The table below presents all supported frameworks by FlagCX and their related communication operations, where the `batch_XXX` and `XXX_coalesced` ops refer to the usage of group primitives.

| Framework                         | PyTorch                      | PaddlePaddle |
|:----------------------------------|:-----------------------------|:-------------|
| send                              | ✓                            |✓             |
| recv                              | ✓                            |✓             |
| batch_isend_irecv                 | ✓                            |✓             |
| broadcast                         | ✓                            |✓             |
| all_reduce                        | ✓                            |✓             |
| all_reduce_coalesced              | ✓ (in order, no aggregation) |✘             |
| reduce                            | ✓                            |✓             |
| all_gather                        | ✓                            |✓             |
| all_gather_into_tensor_coalesced  | ✓ (in order, no aggregation) |✘             |
| gather                            | ✓                            |✓             |
| scatter                           | ✓                            |✓             |
| reduce_scatter                    | ✓                            |✓             |
| reduce_scatter_tensor_coalesced   | ✓ (in order, no aggregation) |✘             |
| all_to_all                        | ✓                            |✓             |
| all_to_all_single                 | ✓                            |✓             |
| barrier                           | ✓                            |✓             |

## Quick Start

### Build 
1. Clone the repository:
    ```sh
    git clone https://github.com/FlagOpen/FlagCX.git
    ```

2. Build the library with different flags targeting to different platforms:
    ```sh
    cd FlagCX
    make [USE_NVIDIA/USE_ILUVATAR_COREX/USE_CAMBRICON/USE_GLOO/USE_MPI/USE_METAX/USE_MUSA/USE_KUNLUNXIN/USE_DU/USE_ASCEND]=1

    # If the compilation variable "make USE_KUNLUNXIN=1" is used, the following environment variables need to be enabled:
    export XPU_FORCE_SHARED_DEVICE_CONTEXT=1
    ```
    The default install path is set to `build/`, you can manually set `BUILDDIR` to specify the build path. You may also define `DEVICE_HOME` and `CCL_HOME` to indicate the install paths of device runtime and communication libraries.

### Tests

Tests for FlagCX are maintained in `test/perf`.
```sh
cd test/perf
make [USE_NVIDIA/USE_ILUVATAR_COREX/USE_CAMBRICON/USE_METAX/USE_MUSA/USE_KUNLUNXIN/USE_DU/USE_ASCEND]=1
mpirun --allow-run-as-root -np 8 ./test_allreduce -b 128K -e 4G -f 2
```
Note that the default MPI install path is set to `/usr/local/mpi`, you may specify the MPI path with:
```sh
make MPI_HOME=<path to mpi install>
```

All tests support the same set of arguments:

* Sizes to scan
  * `-b <min size in bytes>` minimum size to start with. Default: 1M.
  * `-e <max size in bytes>` maximum size to end at. Default: 1G.
  * `-f <increment factor>` multiplication factor between sizes. Default: 2.
* Performance
  * `-w, <warmup iteration count>` number of warmup iterations (not timed). Default: 5.
  * `-n, <iteration count>` number of iterations. Default: 20.
* Utils
  * `-p, <0/1>` print buffer info. Default: 0.
  * `-h` print help message. Default: disabled.

### Training Models
After building and testing FlagCX, you can start training models using upper-layer deep learning frameworks such as PyTorch or PaddlePaddle with FlagCX as communication backend. We provide detailed user guides for both **homogeneous** and **heterogeneous** training across different hardware platforms. Please refer to the docs below:  
📖 [Training Models with Paddle and FlagCX](docs/paddle/README.md)
## License

This project is licensed under the [Apache License (Version 2.0)](https://github.com/FlagOpen/FlagCX/blob/main/LICENSE).
