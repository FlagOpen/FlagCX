#ifndef FLAGCX_MLX5DV_SYMBOLS_H_
#define FLAGCX_MLX5DV_SYMBOLS_H_

#ifdef USE_BUILD_MLX5DV
#include <infiniband/mlx5dv.h>
#else
#include "mlx5/mlx5dvcore.h"
#endif

#include "flagcx.h"

/* MLX5 Direct Verbs Function Pointers*/
struct flagcxMlx5dvSymbols {
  bool (*mlx5dv_internal_is_supported)(struct ibv_device *device);
  int (*mlx5dv_internal_get_data_direct_sysfs_path)(struct ibv_context *context,
                                                    char *buf, size_t buf_len);
  /* DMA-BUF support */
  struct ibv_mr *(*mlx5dv_internal_reg_dmabuf_mr)(struct ibv_pd *pd,
                                                  uint64_t offset,
                                                  size_t length, uint64_t iova,
                                                  int fd, int access,
                                                  int mlx5_access);
};

/* Constructs MLX5 direct verbs symbols per rdma-core linking or dynamic loading
 * mode */
flagcxResult_t buildMlx5dvSymbols(struct flagcxMlx5dvSymbols *mlx5dvSymbols);

#endif // NCCL_MLX5DV_SYMBOLS_H_
