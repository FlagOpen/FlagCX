USE_NVIDIA ?= 0
USE_ILUVATAR_COREX ?= 0
USE_CAMBRICON ?= 0
ifeq ($(USE_CAMBRICON), 1)
   DEV="MLU"
else
   DEV="CUDA"
endif

MPI_HOME ?= /usr/local/mpi
MPI_INCLUDE = $(MPI_HOME)/include
MPI_LIB = $(MPI_HOME)/lib
MPI_LINK = -lmpi
COMPILER = g++
EXTRA_COMPILER_FLAG = -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wl,-rpath,../../build/lib -std=c++11 -g

INCLUDEDIR := $(abspath include)
LIBSRCFILES:= $(wildcard *.cc)

all: test-sendrecv test-allreduce test-allgather test-alltoall test-core-sendrecv

test-sendrecv: test_sendrecv.cpp
	@echo "Compiling $@"
	@$(COMPILER) $(EXTRA_COMPILER_FLAG) -o test_sendrecv test_sendrecv.cpp $(LIBSRCFILES) -I../../flagcx/include -I$(INCLUDEDIR) -I$(MPI_INCLUDE) -L../../build/lib -L$(MPI_LIB) -lflagcx $(MPI_LINK)

test-allreduce: test_allreduce.cpp
	@echo "Compiling $@"
	@$(COMPILER) $(EXTRA_COMPILER_FLAG) -o test_allreduce test_allreduce.cpp $(LIBSRCFILES) -I../../flagcx/include -I$(INCLUDEDIR) -I$(MPI_INCLUDE) -L../../build/lib -L$(MPI_LIB) -lflagcx $(MPI_LINK)

test-allgather: test_allgather.cpp
	@echo "Compiling $@"
	@$(COMPILER) $(EXTRA_COMPILER_FLAG) -o test_allgather test_allgather.cpp $(LIBSRCFILES) -I../../flagcx/include -I$(INCLUDEDIR) -I$(MPI_INCLUDE) -L../../build/lib -L$(MPI_LIB) -lflagcx $(MPI_LINK)

test-alltoall: test_alltoall.cpp
	@echo "Compiling $@"
	@$(COMPILER) $(EXTRA_COMPILER_FLAG) -o test_alltoall test_alltoall.cpp $(LIBSRCFILES) -I../../flagcx/include -I$(INCLUDEDIR) -I$(MPI_INCLUDE) -L../../build/lib -L$(MPI_LIB) -lflagcx $(MPI_LINK)

test-core-sendrecv: test_core_sendrecv.cpp
	@echo "Compiling $@"
	@$(COMPILER) $(EXTRA_COMPILER_FLAG) -o test_core_sendrecv test_core_sendrecv.cpp $(LIBSRCFILES) -I../../flagcx/include -I../../flagcx/service -I../../flagcx/core -I$(INCLUDEDIR) -I$(MPI_INCLUDE) -L../../build/lib/ -L$(MPI_LIB) -lflagcx $(MPI_LINK)

clean:
	@rm -f test_sendrecv
	@rm -f test_allreduce
	@rm -f test_allgather
	@rm -f test_alltoall
	@rm -f test_core_sendrecv

run-sendrecv:
	@mpirun --allow-run-as-root --hostfile ./hostfile -np 2 -x UCX_POSIX_USE_PROC_LINK=n -x ${DEV}_VISIBLE_DEVICES=2 -x NCCL_IB_HCA=mlx5_2 -x NCCL_DEBUG=INFO -x NCCL_DEBUG_SUBSYS=INIT,NET -x NCCL_IB_MERGE_NICS=1 -x NCCL_NET_GDR_LEVEL=4 -x NCCL_IB_QPS_PER_CONNECTION=4 -x NCCL_TOPO_DUMP_FILE=./topo ./test_sendrecv

run-allreduce:
	@mpirun --allow-run-as-root -np 8 -x UCX_POSIX_USE_PROC_LINK=n -x ${DEV}_VISIBLE_DEVICES=0,1,2,3,4,5,6,7 -x FLAGCX_DEBUG=INFO -x FLAGCX_DEBUG_SUBSYS=ALL ./test_allreduce

run-allgather:
	@mpirun --allow-run-as-root -np 8 -x UCX_POSIX_USE_PROC_LINK=n -x ${DEV}_VISIBLE_DEVICES=0,1,2,3,4,5,6,7 -x FLAGCX_DEBUG=INFO -x FLAGCX_DEBUG_SUBSYS=ALL ./test_allgather

run-alltoall:
	@mpirun --allow-run-as-root -np 8 -x UCX_POSIX_USE_PROC_LINK=n -x ${DEV}_VISIBLE_DEVICES=0,1,2,3,4,5,6,7 -x NCCL_DEBUG=INFO -x NCCL_DEBUG_SUBSYS=INIT -x NCCL_COMM_ID=10.8.60.201:8080 ./test_alltoall

run-core-sendrecv:
	@mpirun --allow-run-as-root -np 2 -x UCX_POSIX_USE_PROC_LINK=n -x ${DEV}_VISIBLE_DEVICES=0,1 -x NCCL_IB_HCA=mlx5_2 -x FLAGCX_DEBUG=INFO -x FLAGCX_DEBUG_SUBSYS=INIT,NET -x FLAGCX_TOPO_DUMP_FILE=./topo ./test_core_sendrecv

print_var:
	@echo "USE_NVIDIA: $(USE_NVIDIA)"
	@echo "USE_ILUVATAR_COREX: $(USE_ILUVATAR_COREX)"
	@echo "USE_CAMBRICON: $(USE_CAMBRICON)"
	@echo "MPI_INCLUDE: $(MPI_INCLUDE)"
	@echo "MPI_LIB: $(MPI_LIB)"
	@echo "COMPILER: $(COMPILER)"
	@echo "EXTRA_COMPILER_FLAG: $(EXTRA_COMPILER_FLAG)"
