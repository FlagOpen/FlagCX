[<img src="flagopen.png">](https://flagopen.baai.ac.cn/)

## About

[FlagCX](https://github.com/FlagOpen/FlagCX.git) is a scalable and adaptive cross-chip communication library developed with the backing of the Beijing Academy of Artificial Intelligence (BAAI).

FlagCX is also a part of [FlagAI-Open](https://flagopen.baai.ac.cn/), an open-source initiative by BAAI that aims to foster an open-source ecosystem for AI technologies. It serves as a platform where developers, researchers, and AI enthusiasts can collaborate on various AI projects, contribute to the development of cutting-edge AI solutions, and share their work with the global community.

FlagCX leverages native collective communications libraries to provide the full support of single-chip communications on different platforms. In addition to its native x-CCL support, FlagCX provides an original device-buffer RDMA design to offer advanced support for cross-chip high-performance sendrecev operations (`CORE` module), which can also be integrated with native x-CCL backends to enable optimized cross-chip collective communications. A comprehensive list of currently supported communication backends and their different capabilities are listed as follows:

| Backend       | NCCL | IXCCL  | CNCL | GLOO    | CORE+x-CCL |
|:--------------|:-----|:-------|:-----|:--------|:-----------|
| Mode          | Homo | Homo   | Homo | Hetero  | Hetero     |
| send          | ✓    | ✓      | ✓    | ✓       | ✓          |
| recv          | ✓    | ✓      | ✓    | ✓       | ✓          |
| broadcast     | ✓    | ✓      | ✓    | ✘       | ✓          |
| gather        | ✓    | ✓      | ✓    | ✘       | ✓          |
| scatter       | ✓    | ✓      | ✓    | ✘       | ✓          |
| reduce        | ✓    | ✓      | ✓    | ✘       | ✓          |
| allreduce     | ✓    | ✓      | ✓    | ✓       | ✓          |
| allgather     | ✓    | ✓      | ✓    | ✓       | ✓          |
| reducescatter | ✓    | ✓      | ✓    | ✘       | ✓          |
| alltoall      | ✓    | ✓      | ✓    | ✓       | ✓          |
| group ops     | ✓    | ✓      | ✓    | ?       | ?          |

Note that `Homo` and `Hetero` modes refer to communications among homogeneous and heterogeneous clusters. All supported native collective communication libraries can be referenced through the links below:

- [NCCL](https://github.com/NVIDIA/nccl), NVIDIA Collective Communications Library.
- [IXCCL](https://www.iluvatar.com/software?fullCode=cpjs-rj-rjz), Iluvatar Corex Collective Communications Library.
- [CNCL](https://www.cambricon.com/docs/sdk_1.7.0/cncl_1.2.1/user_guide/index.html#), Cambricon Communications Library.
- [GLOO](https://github.com/facebookincubator/gloo), Gloo Collective Communications Library.

FlagCX also develops plugins to integrate with upper-layer applications such as PyTorch based on its unified APIs. The table below presents the communication operations currently supported by the plugins of their corresponding frameworks, where the `batch_XXX` and `XXX_coalesced` ops refer to the usage of group primitives.

| Plugin                            | PyTorch |
|:----------------------------------|:--------|
| send                              | ✓       |
| recv                              | ✓       |
| batch_isend_irecv                 | ✓       |
| broadcast                         | ✓       |
| all_reduce                        | ✓       |
| all_reduce_coalesced              | ✘       |
| reduce                            | ✓       |
| all_gather                        | ✓       |
| all_gather_into_tensor_coalesced  | ✘       |
| gather                            | ✓       |
| scatter                           | ✓       |
| reduce_scatter                    | ✓       |
| reduce_scatter_tensor_coalesced   | ✘       |
| all_to_all                        | ✓       |

## Quick Start

### Build 
1. Clone the repository:
    ```sh
    git clone https://github.com/FlagOpen/FlagCX.git
    ```

2. Build the library with different flags targeting to different platforms:
    ```sh
    cd FlagCX
    make [USE_NVIDIA/USE_ILUVATAR_COREX/USE_CAMBRICON/USE_GLOO]=1
    ```
    The default install path is set to `build/`, you can manually set `BUILDDIR` to specify the build path. You may also define `DEVICE_HOME` and `CCL_HOME` to indicate the install paths of device runtime and communication libraries.

### Tests

Tests for FlagCX are maintained in `test/perf`.
```sh
cd test/perf
make
./test_allreduce -b 128M -e 8G -f 2
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

## License

This project is licensed under the [Apache License (Version 2.0)](https://github.com/FlagOpen/FlagCX/blob/main/LICENSE).
