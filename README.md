[<img src="flagopen.png">](https://flagopen.baai.ac.cn/)

## About

[FlagCX](https://github.com/FlagOpen/FlagCX.git) is a scalable and adaptive cross-chip communication library developed with the backing of the Beijing Academy of Artificial Intelligence (BAAI).

FlagCX is also a part of [FlagAI-Open](https://flagopen.baai.ac.cn/), an open-source initiative by BAAI that aims to foster an open-source ecosystem for AI technologies. It serves as a platform where developers, researchers, and AI enthusiasts can collaborate on various AI projects, contribute to the development of cutting-edge AI solutions, and share their work with the global community.

FlagCX leverages native collective communication libraries to provide the full support of single-chip communication on different platforms. All supported communication backends are listed as follows:
- [NCCL](https://github.com/NVIDIA/nccl), NVIDIA Collective Communications Library.
- [IXCCL](https://www.iluvatar.com/software?fullCode=cpjs-rj-rjz), Iluvatar Corex Collective Communications Library.
- [CNCL](https://www.cambricon.com/docs/sdk_1.7.0/cncl_1.2.1/user_guide/index.html#), Cambricon Communications Library.

## Quick Start

### Build 
1. Clone the repository:
    ```sh
    git clone https://github.com/FlagOpen/FlagCX.git
    ```

2. Build the library with different flags targeting to different platforms:
    ```sh
    cd FlagCX
    make [USE_NVIDIA/USE_ILUVATAR_COREX/USE_CAMBRICON]=1
    ```
    You should ensure that the corresponding native collective communication librariy has already been built.

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

## License

This project is licensed under the [Apache License (Version 2.0)](https://github.com/FlagOpen/FlagCX/blob/main/LICENSE).