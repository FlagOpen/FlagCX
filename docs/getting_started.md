## Environment Setup

**Reference Command for Container Creation:** Modify the `<Docker name> <mount directory> <mount point>` and choose the `<Docker Image>` as needed.

```Plain
-------------Reference Command for Container Creation on NVIDIA A800 Platform------------
sudo docker run -itd \
                --name <Docker name> \
                --privileged \
                --net=host \
                --pid=host \
                --cap-add=ALL \
                --shm-size 128G \
                --ulimit memlock=-1 \
                --gpus all \
                -v /dev/:/dev/ \
                -v /usr/src/:/usr/src/ \
                -v /lib/modules/:/lib/modules/ \
                -v <mount directory>:<mount point> \
                <Docker Image> \
                /bin/bash
-------------Reference Command for Container Creation on Kunlunxin P800 Platform-----------
sudo docker run -itd \
        --name <Docker name> \
        --privileged \
        --net=host \
        --pid=host \
        --shm-size 128G \
        --ulimit memlock=-1 \
        --group-add video \
        -v <mount directory>:<mount point> \
        -v /usr/local/xpu/:/usr/local/xpu \
        <Docker Image> \
        /bin/bash  
```

​            

## FlagCX Build and Installation

1. Obtain FlagCX Source Code and Build Installation

   Review and Choose Build Options Suitable for the Current Platform

   ```
   git clone https://github.com/FlagOpen/FlagCX.git
   cd FlagCX 
   cat Makefile
   make USE_NVIDIA=1 -j$(nproc) # NVIDIA GPU Platform
   make USE_CAMBRICON=1 -j$(nproc)  # Cambricon Platform
   make USE_KUNLUNXIN=1 -j$(nproc) # Kunlunxin Platform
   make USE_NVIDIA=1 USE_CAMBRICON=1 ... -j$(nproc) # Multi-Platform Hybrid Build (e.g., supporting NVIDIA and Cambricon)
   ```

2. Successful Build Result

   ![FlagCX_Build_and_Installation_Successful_Build_Result.PNG](https://github.com/whollo/FlagCX/blob/add-flagcx-wuh/docs/images/FlagCX_Build_and_Installation_Successful_Build_Result.PNG)

3. Potential Issues During Compilation

   - **If** **`nccl.h`** **or other libraries are not found** 

      - You can first use `locate xxx.h` to find the local path of the header file.

      - Once found, update the corresponding library/include paths in the `Makefile`.

      - If the file is not present locally, install the corresponding header/library. There are multiple installation methods; one example is provided below.

        ```Plain
        sudo apt-key adv --keyserver keyserver.ubuntu.com --recv-keys A4B469963BF863CC  #Import the public key
        echo "deb https://developer.download.nvidia.com/compute/cuda/repos/ubuntu2004/x86_64/ /" | sudo tee /etc/apt/sources.list.d/cuda.list                                # Add repository
        sudo apt-get update                                              # Update apt
        sudo apt-get install libnccl-dev                                 # Install NCCL development package
        sudo apt-get install cuda-toolkit-11-8                           # Install CUDA Toolkit
        ```


   - If `gtest.h` header file error occurs

      Install Google Test (gtest) unit testing framework

      ```Plain
      git clone https://github.com/google/googletest.git # Obtain gtest source code
      cd googletest                                      # Enter the project directory
      mkdir build                                        # Create a directory to store build output
      cd build                                           # Enter the build directory
      cmake ..                                           # Generate native build scripts for GoogleTest
      make                                               # Compile the source
      sudo make install                                  # Install the static libraries
      ```


## Homogeneous Testing with FlagCX

### Communication API Test

1. Environment Setup

   Select the Docker image `<Docker Image>` to create a container and enter it.

2. Select Build Options

   ```Plain
   cd FlagCX/test/perf # Enter the test directory
   make USE_NVIDIA=1 # Compile with options based on the hardware platform
   ```

3. Successful Build Result

   ![Homogeneous_Communication_API_Test_Successful_Build_Result.png](https://github.com/whollo/FlagCX/blob/add-flagcx-wuh/docs/images/Homogeneous_Communication_API_Test_Successful_Build_Result.png)

### Torch API Test

1. Environment Setup

   - Select the Docker image `<Docker Image>` to create a container and enter it.
   
   
      - Check whether PyTorch is installed in the container. If not, install it using the procedure below.
   
        ```Plain
        pip list         # Check if PyTorch is installed in the container
        ------------------# Install PyTorch----------------------------------
        python -m pip install --pre torch \
          --index-url https://download.pytorch.org/whl/nightly/<cuXXX> \
          --trusted-host download.pytorch.org \
          --no-cache-dir --timeout 300 \
          --root-user-action=ignore
          
        nvcc --version 2>/dev/null || nvidia-smi   # Check the CUDA runtime driver version; if release is 12.4, install the corresponding cu124 version
        ```
   
         **Note:** `<cuXXX>` corresponds to the version of the CUDA toolkit matching your current hardware driver.
   


2. Select Build Options

   ```
   cd /FlagCX/plugin/torch/
   python setup.py develop --adaptor [xxx]
   ```

   **Note**: `[xxx]` should be selected according to the current platform, e.g., `nvidia`, `klx`, etc.

3. Successful Build Result

   - After compilation, a `build` directory will be generated.Run the command:

     ```
     pip list | grep flagcx
     ```

   - You should see the `flagcx` version and path matching the current build directory, indicating that the compilation and installation were successful.

     ![Homogeneous Torch API Successful Build Result.png](https://github.com/whollo/FlagCX/blob/add-flagcx-wuh/docs/images/Homogeneous_Torch_API_Successful_Build_Result.png)


### FlagCX LLaMA3-8B Training on NVIDIA GPUs using FlagScale

1. Environment Setup

   - Select the Docker image `<Docker Image>`, create a container named `<Docker name>`, and enter the container.

     ```Plain
     ## Check conda environments
     conda env list  
     ## Activate the flagscale-train environment for subsequent operations
     conda activate flagscale-train 
     pip install modelscope 
     pip install pandas
     ```


2. Build and Install the FlagCX Library

   - Pull the FlagScale and FlagCX source code.

     ```Plain
     git clone https://github.com/FlagOpen/FlagScale.git 
     git clone https://github.com/FlagOpen/FlagCX.git
     ```
   
   
      - Build and Install FlagCX
   
        ```Plain
        cd FlagCX                        # Enter the FlagCX directory; check the Makefile to select build options based on your platform, e.g., USE_NVIDIA, USE_KUNLUNXIN, etc.
        make USE_NVIDIA=1                # Compile FlagCX with NVIDIA GPU support
        cd plugin/torch                  # Enter the Torch plugin directory
        python setup.py develop --adaptor nvidia   # Install the Python Torch adaptor in development mode
        pip list | grep flagcx           # Verify the installation and check the absolute path of the installed FlagCX package
        ```
   
   
      - Screenshot of Successful Compilation and Installation
   
        ![FlagCX LLaMA3-8B Screenshot of Successful Compilation and Installation.png](https://github.com/whollo/FlagCX/blob/add-flagcx-wuh/docs/images/FlagCX_LLaMA3-8B_Screenshot_of_Successful_Compilation_and_Installation.png)
   


## Heterogeneous Testing with FlagCX

### Communication API Test

1. Environment Setup

   - Setting up Passwordless SSH Between Two Hosts

     -  Assume there are two hosts: `root@<ip1>` and `root@<ip2>`. The following steps configure mutual passwordless SSH login.

     - Generate SSH key on each host

       ```Bash
       ssh-keygen -t rsa   # Press Enter for all prompts
       cd /root/.ssh       # Enter the .ssh directory
       cat id_rsa.pub >> authorized_keys   # Add public key to authorized_keys
       chmod 700 ~/.ssh    # Set correct permissions for .ssh
       chmod 600 authorized_keys   # Set correct permissions for authorized_keys`
       ```

     - Configure host1 to login to host2 without password

       - On host1, view the public key:

          ```
          cat /root/.ssh/id_rsa.pu
          ```

       - Copy the public key and append it to host2’s `authorized_keys`:

          ```
          vi /root/.ssh/authorized_keys   # Paste host1 public key here
          ```

       - Edit SSH server configuration on host2 (and host1 similarly):

          ```Plain
          vi /etc/ssh/sshd_config
              # Set the following:
              Port 8010
              PermitRootLogin yes
              PubkeyAuthentication yes
              AuthorizedKeysFile /root/.ssh/authorized_keys
              PasswordAuthentication no
          :wq   # Save and exit
          ```

       - Restart SSH service:

          ```
          service ssh restart
          ```

       - Test passwordless SSH from host1 to host2:

          ```
          ssh -p <port> root@<ip2>
          ```

           If login succeeds without a password, the configuration is effective.

     -  Configure host2 to login to host1

       Repeat the same steps, but append host2’s public key to host1’s `authorized_keys`.


2. Create a symbolic link

   ```
   cd /root
   ln -s /workspace/flagcx_test_[xxx]/FlagCX ./FlagCX
   ```

   **Explanation**

   - `/workspace/` — Shared folder; after mounting in the containers, both host1 and host2 can access it.
   - `flagcx_test_[xxx]/FlagCX` — `[xxx]` indicates the FlagCX communication library for different platforms.
   - `./FlagCX` — Symbolic link name; by accessing `/root/FlagCX`, you can reach the FlagCX library for the current platform.

3. Build and Install

   - On each host, compile and install the FlagCX communication API separately.

   - Refer to the section **“Homogeneous Testing with FlagCX”** for detailed steps.