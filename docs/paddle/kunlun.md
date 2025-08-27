# Guide For Using Paddle With FlagCX On XPU Machines
## Environment Setup
1. prepare a docker container on XPU machines
2. start the docker container
    ```bash
    sudo docker exec -it [container_name] bash
    ```
3. clone Paddle
    ```bash
    git clone https://github.com/PaddlePaddle/Paddle.git
    ```
## Compile Paddle With FlagCX
Please follow the following commands
```bash
# checkout develop branch
cd Paddle && git checkout develop
# create build directory
mkdir build && cd build
# install paddle dependencies
pip install -r ../python/requirements.txt
# run cmake 
cmake .. -GNinja -DPY_VERSION=3.10 -DCMAKE_BUILD_TYPE=Release \
     -DWITH_GPU=OFF \
     -DWITH_XPU=ON \
     -DON_INFER=OFF \
     -DWITH_PYTHON=ON \
     -DWITH_XPU_XRE5=ON \
     -DWITH_MKL=ON \
     -DWITH_XPU_BKCL=ON \
     -DWITH_FLAGCX=ON \
     -DWITH_TESTING=OFF \
     -DWITH_DISTRIBUTE=ON \
     -DWITH_XPTI=OFF \
     -DBUILD_WHL_PACKAGE=ON \
     -DWITH_XPU_XFT=OFF

# compile
ninja -j$(nproc)
# locate paddle whl package
cd ./python/dist
# install whl package
pip install -U [whl_package_name]
# check if installation was successful
python -c "import paddle;paddle.utils.run_check()"
```

## Train Model using Paddle + FlagCX
We now support training GPT3 on XPU machines using Paddle + FlagCX. Please refer to the following steps to get started
1. clone PaddleNLP
    ```bash
    git clone https://github.com/PaddlePaddle/PaddleNLP.git
    ```
2. install dependencies
    ```bash
    pip install -r requirements.txt
    pip install -r requirements-dev.txt
    ```
3. download data
    ```bash
    # create data repository
    mkdir -p ./llm/data 
    cd ./llm/data

    # download data
    wget https://bj.bcebos.com/paddlenlp/models/transformers/gpt/data/gpt2_openwebtext_100k.bin
    wget https://bj.bcebos.com/paddlenlp/models/transformers/gpt/data/gpt2_openwebtext_100k.idx 
    ```
4. prepare training script  
    please refer to the following script for training GPT3
    ```bash
    # this is the script for training gpt3 on XPU machines using flagcx as communication backend
    # define root path
    export root_path=/workspace
    export PYTHONPATH=$root_path/PaddleNLP:$PYTHONPATH
    export PADDLE_DISTRI_BACKEND=flagcx

    # log
    export GLOG_v=0
    export FLAGCX_DEBUG=INFO
    export FLAGCX_DEBUG_SUBSYS=INIT
    export XPU_FORCE_SHARED_DEVICE_CONTEXT=1

    current_date=$(date +"%m%d")
    task_name="gpt13b_dynamic_hand_nosp_ly4_debug_$current_date"
    log_dir="log_$current_date/${task_name}_1"
    output_dir="output_$current_date/${task_name}_1"

    rm -rf ${log_dir}
    rm -rf ${output_dir}


    python -u -m paddle.distributed.launch \
        --xpus "0,1,2,3,4,5,6,7" \
        --log_dir ${log_dir} \
        run_pretrain.py \
        ${root_path}/PaddleNLP/tests/test_tipc/dygraph/hybrid_parallelism/gpt3/auto_config_gpt3_13b/pretrain-gpt3_13b-config.json

    echo "---- $task_name performance:"
    echo "throughput(tokens/s/card):"
    cat ${log_dir}/workerlog.0 | grep "interval_tokens_per_second_per_device:" | awk -F ',' '{print $11}' | awk -F ' ' '{print $2}' | awk 'NR > 10 {print $1}' |sort -n | awk '{values[NR] = $1} END {for (i = 3; i <= NR-2; i++) sum += values[i]; print sum / (NR-4)}'

    echo "max_memory_allocated(GB):"
    cat ${log_dir}/workerlog.0 | grep "interval_tokens_per_second_per_device:" | awk -F ',' '{print $7}' | tail -n 1

    echo "max_memory_reserved(GB):"
    cat ${log_dir}/workerlog.0 | grep "interval_tokens_per_second_per_device:" | awk -F ',' '{print $8}' | tail -n 1
    ```
    __Note__:  
    To train model using xpu, we need to specify the device type in model config json file:
    - `"device": "xpu"`