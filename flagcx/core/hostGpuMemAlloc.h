#ifndef HOST_GPU_MEMALLOC_H
#define HOST_GPU_MEMALLOC_H
/*
 * nvcc memAlloc.cu -o mem -lcuda  -I /usr/local/cuda/include
 */
#include <stdlib.h>
#include <getopt.h>
#include <memory.h>
#include <stdio.h>
#include <math.h>
#include <iostream>
#include <iomanip>
#include <unistd.h>
#include "flagcx.h"

#ifdef CREATE_GPU_MEMALLOC_API
#define GPU_MEMALLOC_API_EXTERN
#else
#define GPU_MEMALLOC_API_EXTERN extern
#endif


struct DIM3{
    unsigned int x;
    unsigned int y;
    unsigned int z;
};

struct hostLaunchArgs{
    volatile bool stopLaunch;
    volatile bool retLaunch;
};

void cpuAsyncLaunch(void *args);
flagcxResult_t flagcxLaunchKernel(void *func, DIM3 grid, DIM3 block, void **args, size_t share_mem, void *stream, void *memHandle);

GPU_MEMALLOC_API_EXTERN void **flagcxDevKernelFunc;

/**
 * @brief Initializes the specified GPU device and sets up the memory handle.
 *
 * This function initializes the specified GPU device and prepares the associated memory handle. 
 * By providing the device ID, use the device number specified by dev_id for subsequent operations, 
 * and the `memHandle` will store the memory handle associated with this device for subsequent memory management operations.
 *
 * @param[in]  dev_id    The device ID to initialize. This specifies the GPU device to be used.
 * @param[out] memHandle Pointer to store the memory handle associated with the device.
 *
 * @return Returns 0 on success, or a non-zero error code on failure.
 */
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxCuInit)(int dev_id, void **memHandle);

/**
 * @brief Cleans up and releases resources associated with the specified GPU device.
 *
 * This function destroys the resources associated with the specified GPU device, identified by `dev`. 
 * It also handles the cleanup of the memory handle provided in `memHandle`. 
 * This is typically called when the GPU resources are no longer needed.
 *
 * @param[in] dev       The device ID of the GPU whose resources are to be destroyed.
 * @param[in] memHandle The memory handle associated with the GPU device that needs to be released.
 *
 * @return Returns 0 on success, or a non-zero error code on failure.
 */
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxCuDestroy)(int dev, void *memHandle);

/**
 * @brief Allocates a block of memory accessible to both RDMA and GPU.
 *
 * This function allocates a block of memory of the specified `size` and stores its address in the `ptr` pointer.
 * The allocated memory can be accessed by both RDMA and GPU. The `memHandle` parameter is used to provide
 * additional input and output handles.
 *
 * @param[out] ptr       Pointer to store the allocated memory address.
 * @param[in]  size      Size of the memory block to allocate, in bytes.
 * @param[in,out] memHandle Handle for additional input and output parameters.
 *
 * @return Returns 0 on success, or a non-zero error code on failure.
 */
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxCuGdrMemAlloc)(void **ptr, size_t size, void *memHandle);

/**
 * @brief Frees a block of memory previously allocated and accessible to RDMA and GPU.
 *
 * This function releases a block of memory that was previously allocated using `flagcxCuGdrMemAlloc`. 
 * The `ptr` parameter points to the memory to be freed, and `memHandle` may be used for 
 * additional input and output handles associated with the memory block.
 *
 * @param[in] ptr       Pointer to the memory block to be freed.
 * @param[in,out] memHandle Handle for additional input and output parameters, if applicable.
 *
 * @return Returns 0 on success, or a non-zero error code on failure.
 */
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxCuGdrMemFree)(void *ptr, void *memHandle);

/**
 * @brief Creates a CUDA stream and associates it with a memory handle.
 * 
 * This function creates a CUDA stream for asynchronous operations on the device
 * and potentially associates it with a memory handle if required. The stream can be
 * used to perform asynchronous tasks like memory copy, kernel launches, etc., in the GPU.
 * 
 * @param[out] stream    A pointer to the location where the created stream handle will be stored.
 *                       This pointer will be set to the created CUDA stream.
 * 
 * @param[in]  memHandle A memory handle that can be associated with the stream for specific operations.
 *                       If not used, this parameter can be NULL or ignored, depending on the implementation.
 * 
 * @return int           Returns 0 (flagcxSuccess) if the stream was created successfully,
 *                       or an error code if there was a failure.
 * 
 * @note The caller is responsible for destroying the created stream using `cudaStreamDestroy`
 *       when it is no longer needed to avoid resource leaks.
 */
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxDeviceCreateStream)(void **stream);

/**
 * @brief Destroys a previously created CUDA stream and optionally disassociates it from a memory handle.
 * 
 * This function destroys a CUDA stream that was previously created, ensuring that all operations 
 * in the stream are completed before the stream is destroyed. If a memory handle was associated 
 * with the stream, the function may also handle the necessary cleanup or disassociation.
 * 
 * @param[in] stream     The CUDA stream to be destroyed. This should be a valid stream created 
 *                       by `flagcxDeviceCreateStream` or equivalent.
 * 
 * @param[in] memHandle  A memory handle that may be associated with the stream. This parameter can 
 *                       be used to handle any specific disassociation or cleanup if needed. 
 *                       It can be NULL if no specific memory handle is associated.
 * 
 * @return int           Returns 0 (flagcxSuccess) if the stream was successfully destroyed, 
 *                       or an error code if there was a failure.
 * 
 * @note The function ensures that all tasks queued in the stream are completed before the 
 *       stream is destroyed. The caller should ensure that the stream is no longer in use 
 *       before calling this function.
 */
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxDeviceDestroyStream)(void *stream);

/**
 * @brief Synchronizes a CUDA stream, ensuring all tasks submitted to the stream are complete.
 * 
 * This function blocks the host until all tasks currently queued in the specified CUDA stream 
 * have been completed. If a memory handle is associated with the stream, the function may also 
 * perform additional operations related to memory synchronization or cleanup.
 * 
 * @param[in] stream     The CUDA stream to be synchronized. This should be a valid stream created 
 *                       by `flagcxDeviceCreateStream` or equivalent.
 * 
 * @param[in] memHandle  A memory handle that may be associated with the stream. This parameter can 
 *                       be used for memory-related operations, such as synchronization or cleanup 
 *                       tasks. It can be NULL if no specific memory handle is associated.
 * 
 * @return int           Returns 0 (flagcxSuccess) if the stream was successfully synchronized, 
 *                       or an error code if there was a failure.
 * 
 * @note This function will block the host until the specified stream has completed all pending tasks. 
 *       Ensure that the stream has active tasks to synchronize before calling this function to avoid 
 *       unnecessary blocking.
 */
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxDeviceStreamSynchronize)(void *stream);

/**
 * @brief Launches a CUDA kernel with the specified execution configuration.
 * 
 * This function launches a CUDA kernel with the provided grid and block dimensions, 
 * shared memory size, and stream. The kernel function is executed with the specified 
 * arguments on the device. If a memory handle is provided, it may be used for additional 
 * memory management or synchronization tasks.
 * 
 * @param[in] func       The CUDA kernel function to be launched. This should be a pointer to 
 *                       a compiled device function.
 * 
 * @param[in] block_x    The number of threads in the x-dimension of each block.
 * @param[in] block_y    The number of threads in the y-dimension of each block.
 * @param[in] block_z    The number of threads in the z-dimension of each block.
 * 
 * @param[in] grid_x     The number of blocks in the x-dimension of the grid.
 * @param[in] grid_y     The number of blocks in the y-dimension of the grid.
 * @param[in] grid_z     The number of blocks in the z-dimension of the grid.
 * 
 * @param[in] args       An array of pointers to the arguments to be passed to the CUDA kernel.
 *                       These arguments must match the signature of the kernel function.
 * 
 * @param[in] share_mem  The amount of dynamic shared memory in bytes that the kernel can use 
 *                       during execution. If no additional shared memory is needed, this can be 0.
 * 
 * @param[in] stream     The CUDA stream in which the kernel is to be launched. This allows 
 *                       for asynchronous execution. If no stream is provided, the default stream 
 *                       (0) will be used.
 * 
 * @param[in] memHandle  A memory handle that may be associated with the kernel launch for 
 *                       specific memory operations or optimizations. This parameter can be NULL 
 *                       if not used.
 * 
 * @return void          This function does not return a value. Any errors during the kernel 
 *                       launch should be handled by the caller or through CUDA's error handling 
 *                       mechanisms.
 * 
 * @note The caller must ensure that the grid and block dimensions, as well as the kernel 
 *       arguments, are correctly configured before launching the kernel. Improper configuration 
 *       can result in undefined behavior or kernel launch failure.
 */
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*_flagcxLaunchKernel)(void *func, unsigned int block_x, unsigned int block_y, unsigned int block_z, unsigned int grid_x, unsigned int grid_y, unsigned int grid_z, void **args, size_t share_mem, void *stream, void *memHandle);

/**
 * @brief Allocates shared memory on the host side for use with CUDA operations.
 * 
 * This function is used to allocate a block of shared memory on the host that can be 
 * utilized by CUDA kernels or other CUDA operations. The memory allocated is intended 
 * to be shared among threads within a block or across blocks, depending on the use case.
 * 
 * @param[out] ptr      A pointer to the allocated memory block. This pointer should be 
 *                      allocated with enough space to hold the requested 'size' of shared 
 *                      memory. The pointer will be set to the address of the allocated memory.
 * 
 * @param[in]  size     The size in bytes of the shared memory block to be allocated. This 
 *                      size must be sufficient to accommodate the needs of the CUDA 
 *                      operations that will use this memory.
 * 
 * @param[in]  memHandle A memory handle that can be used to associate the allocated memory 
 *                      with specific memory operations or optimizations. This handle can 
 *                      be NULL if no specific memory operations are required.
 * 
 * @return void          This function does not return a value. The allocated memory is 
 *                      accessed through the 'ptr' parameter. Any errors during memory 
 *                      allocation should be handled by the caller.
 * 
 * @note The caller is responsible for ensuring that the allocated memory is appropriately 
 *       sized and aligned for the intended use. Additionally, the caller should manage 
 *       the lifetime of the allocated memory, including freeing it when no longer needed.
 *       The 'memHandle' can be used for more advanced memory management if required.
 */
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxHostShareMemAlloc)(void **ptr, size_t size, void *memHandle);

/**
 * @brief Frees shared memory on the host side that was previously allocated for use with UCCL operations.
 *
 * This function is used to deallocate memory on the host that was allocated for shared use among UCCL operations. 
 * It is crucial to call this function to release the memory once it is no longer required to prevent memory leaks.
 *
 * @param[in] ptr A pointer to the memory block to be freed. This should be a valid pointer to the memory 
 *                that was previously allocated using a corresponding UCCL memory allocation function.
 *
 * @param[in] memHandle A memory handle that was associated with the allocated memory. This handle is used to 
 *                      identify the specific memory block and ensure proper deallocation. It must be the 
 *                      same handle that was used when the memory was allocated.
 *
 * @return void This function does not return a value. The memory pointed to by 'ptr' will be freed. Any errors 
 *         during memory deallocation should be handled by the caller.
 *
 * @note The caller must ensure that the memory is not accessed after it has been freed to avoid undefined behavior. 
 *       It is also the caller's responsibility to track the memory handle associated with the allocated memory 
 *       and pass it correctly to this function to ensure the correct memory block is freed.
 */
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxHostShareMemFree)(void *ptr, void *memHandle);
/**
 * @brief Function pointer type for device synchronization with a memory handle in UCCL.
 *
 * This function pointer type defines a callback for synchronizing a UCCL device, where the synchronization
 * operation is associated with a specific memory handle. This allows for more targeted synchronization,
 * potentially improving performance by synchronizing only the operations related to a particular memory block.
 *
 * @param[in] memHandle A pointer to a memory handle that identifies the memory block or context for which
 *                      synchronization is required. This handle should be obtained during the allocation or
 *                      initialization of the memory block and should be passed to this function to ensure
 *                      the correct synchronization context.
 *
 * @return void This function does not return any value. The action performed is the synchronization of the
 *               UCCL device for the memory context associated with the provided handle. Any errors during
 *               synchronization should be handled internally.
 *
 * @note The actual implementation of the function should be provided by the UCCL library or the user. It should
 *       handle all the necessary steps to synchronize the device for the specific memory context. The function
 *       pointer can be assigned to a specific synchronization function that matches this signature and is
 *       capable of handling the synchronization for the given memory handle.
 */
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxDeviceSynchronize)();

/**
 * @brief Copies memory between two device buffers using a specific memory handle in UCCL.
 *
 * This function pointer defines a callback for copying memory from a source buffer to a destination buffer
 * in the device, where the memory operation is associated with a specific memory handle. The use of the 
 * memory handle ensures the copy operation occurs within the correct memory context.
 *
 * @param[out] dst A pointer to the destination buffer in the device memory where the data will be copied to.
 * @param[in]  src A pointer to the source buffer in the device memory from which the data will be copied.
 * @param[in]  size The size of the data to copy, in bytes.
 * @param[in]  memHandle A pointer to a memory handle that identifies the memory block or context for which
 *                       the memory copy is required. The handle should be obtained during the allocation or
 *                       initialization of the memory block.
 *
 * @return void This function does not return any value. Any errors during the copy process should be handled
 *               internally.
 *
 * @note The actual implementation should ensure the correct and efficient copying of memory between the source 
 *       and destination buffers using the provided memory handle. It should handle device-specific synchronization
 *       if necessary.
 */
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxDeviceMemcpy)(void *dst, void *src, size_t size, flagcxMemcpyType_t type, flagcxStream_t stream, void *args);

GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxDeviceMemset)(void *ptr, int value, size_t size, flagcxMemType_t type);
/**
 * @brief Allocates device memory using a specific memory handle in UCCL.
 *
 * This function pointer defines a callback for allocating memory on the device, where the allocation operation
 * is associated with a specific memory handle. The memory handle ensures the allocation is made in the correct
 * memory context, potentially optimizing memory management.
 *
 * @param[out] ptr A double pointer to the memory location that will store the address of the allocated memory
 *                 on the device.
 * @param[in]  size The size of the memory to allocate, in bytes.
 * @param[in]  memHandle A pointer to a memory handle that identifies the memory context for which the
 *                       allocation is required. The handle should be obtained during initialization of
 *                       the memory block.
 *
 * @return void This function does not return any value. Any errors during memory allocation should be handled
 *               internally.
 *
 * @note The actual implementation should ensure the correct and efficient allocation of memory within the
 *       specified memory context using the memory handle.
 */
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxDeviceMalloc)(void **ptr, size_t size, flagcxMemType_t type);

/**
 * @brief Frees device memory using a specific memory handle in UCCL.
 *
 * This function pointer defines a callback for freeing memory on the device, where the memory free operation
 * is associated with a specific memory handle. The memory handle ensures the correct memory context is used
 * for deallocation.
 *
 * @param[in,out] ptr A double pointer to the memory location on the device that should be freed. After the memory
 *                    is freed, the pointer will be set to `NULL`.
 * @param[in]     memHandle A pointer to a memory handle that identifies the memory context for which the
 *                          memory deallocation is required. The handle should be obtained during the
 *                          initialization of the memory block.
 *
 * @return void This function does not return any value. Any errors during memory deallocation should be handled
 *               internally.
 *
 * @note The actual implementation should ensure that the memory is correctly freed and that any synchronization
 *       or cleanup tasks required by the memory context are handled.
 */
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxDeviceFree)(void *ptr, flagcxMemType_t type);
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxTopoGetSystem)(void *topoArgs, void **system);
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxTopoGetLocalNet)(int gpu, char *name); 
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxSetDevice)(int dev);
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxGetDevice)(int *dev);
GPU_MEMALLOC_API_EXTERN flagcxResult_t *(*flagcxGetVendor)(char *vendor);
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxDeviceStreamQuery)(void *stream);
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxCopyArgsInit)(void **args);
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxCopyArgsFree)(void *args);

GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxDeviceCreateEvent)(void **event, void *memHandle);
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxDeviceEventQuery)(void *event, void *memHandle);
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxDeviceEventBlock)(void *event, void *memHandle);
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxDeviceDestroyEvent)(void *event, void *memHandle);
GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxDeviceEventRecord)(void *event, void *stream, void *memHandle);

GPU_MEMALLOC_API_EXTERN flagcxResult_t (*flagcxDeviceLaunchHostFunc)(void *stream, void (*fn)(void *),  void *args);


#endif
