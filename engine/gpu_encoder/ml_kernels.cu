/**
 * Kolibri ML CUDA Kernels
 * GPU-accelerated ML operations for neural network inference
 */

#include <cuda_runtime.h>
#include <math.h>

#define BLOCK_SIZE 256
#define WARP_SIZE 32

/* Error checking macro */
#define CUDA_CHECK(call) \
    do { \
        cudaError_t err = call; \
        if (err != cudaSuccess) { \
            fprintf(stderr, "CUDA error at %s:%d: %s\n", \
                    __FILE__, __LINE__, cudaGetErrorString(err)); \
            return err; \
        } \
    } while(0)

/**
 * Softmax kernel - numerically stable implementation
 * Input: [batch_size, seq_len, hidden_size]
 * Output: [batch_size, seq_len, hidden_size]
 */
__global__ void ml_softmax_kernel(
    const float* __restrict__ input,
    float* __restrict__ output,
    int batch_size,
    int seq_len,
    int hidden_size
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total_rows = batch_size * seq_len;
    
    if (idx >= total_rows) return;
    
    const float* row = input + idx * hidden_size;
    float* out_row = output + idx * hidden_size;
    
    /* Find maximum for numerical stability */
    float max_val = row[0];
    for (int i = 1; i < hidden_size; i++) {
        max_val = fmaxf(max_val, row[i]);
    }
    
    /* Compute exp and sum */
    float sum = 0.0f;
    for (int i = 0; i < hidden_size; i++) {
        out_row[i] = expf(row[i] - max_val);
        sum += out_row[i];
    }
    
    /* Normalize */
    float inv_sum = 1.0f / sum;
    for (int i = 0; i < hidden_size; i++) {
        out_row[i] *= inv_sum;
    }
}

/**
 * GELU activation kernel
 * Approximation: 0.5 * x * (1 + tanh(sqrt(2/pi) * (x + 0.044715 * x^3)))
 */
__global__ void ml_gelu_kernel(
    const float* __restrict__ input,
    float* __restrict__ output,
    int size
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= size) return;
    
    float x = input[idx];
    float cdf = 0.5f * (1.0f + tanhf(0.7978845608f * (x + 0.044715f * x * x * x)));
    output[idx] = x * cdf;
}

/**
 * Layer normalization kernel
 * Normalizes over the last dimension
 */
__global__ void ml_layer_norm_kernel(
    const float* __restrict__ input,
    const float* __restrict__ gamma,
    const float* __restrict__ beta,
    float* __restrict__ output,
    int batch_size,
    int hidden_size,
    float eps
) {
    int batch_idx = blockIdx.x;
    if (batch_idx >= batch_size) return;
    
    const float* row = input + batch_idx * hidden_size;
    float* out_row = output + batch_idx * hidden_size;
    
    /* Compute mean using shared memory */
    __shared__ float shared_sum[BLOCK_SIZE];
    __shared__ float shared_sq_sum[BLOCK_SIZE];
    
    int tid = threadIdx.x;
    float local_sum = 0.0f;
    float local_sq_sum = 0.0f;
    
    for (int i = tid; i < hidden_size; i += blockDim.x) {
        float val = row[i];
        local_sum += val;
        local_sq_sum += val * val;
    }
    
    shared_sum[tid] = local_sum;
    shared_sq_sum[tid] = local_sq_sum;
    __syncthreads();
    
    /* Reduce within block */
    for (int s = blockDim.x / 2; s > 0; s >>= 1) {
        if (tid < s) {
            shared_sum[tid] += shared_sum[tid + s];
            shared_sq_sum[tid] += shared_sq_sum[tid + s];
        }
        __syncthreads();
    }
    
    float mean = shared_sum[0] / hidden_size;
    float variance = shared_sq_sum[0] / hidden_size - mean * mean;
    float inv_std = rsqrtf(variance + eps);
    
    /* Normalize and apply affine transform */
    for (int i = tid; i < hidden_size; i += blockDim.x) {
        float normalized = (row[i] - mean) * inv_std;
        out_row[i] = gamma[i] * normalized + beta[i];
    }
}

/**
 * Matrix multiplication kernel (simple implementation)
 * C = A * B where A is [M, K] and B is [K, N]
 */
__global__ void ml_matmul_kernel(
    const float* __restrict__ A,
    const float* __restrict__ B,
    float* __restrict__ C,
    int M, int K, int N
) {
    int row = blockIdx.y * blockDim.y + threadIdx.y;
    int col = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (row >= M || col >= N) return;
    
    float sum = 0.0f;
    for (int k = 0; k < K; k++) {
        sum += A[row * K + k] * B[k * N + col];
    }
    C[row * N + col] = sum;
}

/**
 * Tiled matrix multiplication for better performance
 */
#define TILE_SIZE 16

__global__ void ml_matmul_tiled_kernel(
    const float* __restrict__ A,
    const float* __restrict__ B,
    float* __restrict__ C,
    int M, int K, int N
) {
    __shared__ float As[TILE_SIZE][TILE_SIZE];
    __shared__ float Bs[TILE_SIZE][TILE_SIZE];
    
    int bx = blockIdx.x;
    int by = blockIdx.y;
    int tx = threadIdx.x;
    int ty = threadIdx.y;
    
    int row = by * TILE_SIZE + ty;
    int col = bx * TILE_SIZE + tx;
    
    float sum = 0.0f;
    
    for (int t = 0; t < (K + TILE_SIZE - 1) / TILE_SIZE; t++) {
        /* Load tiles into shared memory */
        int k_idx = t * TILE_SIZE + tx;
        As[ty][tx] = (row < M && k_idx < K) ? A[row * K + k_idx] : 0.0f;
        
        k_idx = t * TILE_SIZE + ty;
        Bs[ty][tx] = (k_idx < K && col < N) ? B[k_idx * N + col] : 0.0f;
        
        __syncthreads();
        
        /* Compute partial dot product */
        for (int k = 0; k < TILE_SIZE; k++) {
            sum += As[ty][k] * Bs[k][tx];
        }
        
        __syncthreads();
    }
    
    if (row < M && col < N) {
        C[row * N + col] = sum;
    }
}

/**
 * Attention score computation: Q * K^T / sqrt(d_k)
 */
__global__ void ml_attention_scores_kernel(
    const float* __restrict__ Q,
    const float* __restrict__ K,
    float* __restrict__ scores,
    int batch_size,
    int num_heads,
    int seq_len,
    int head_dim
) {
    int b = blockIdx.z;
    int h = blockIdx.y;
    int q_idx = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (b >= batch_size || h >= num_heads || q_idx >= seq_len) return;
    
    float scale = rsqrtf((float)head_dim);
    
    int qk_offset = (b * num_heads + h) * seq_len * head_dim;
    const float* q_row = Q + qk_offset + q_idx * head_dim;
    float* score_row = scores + (b * num_heads + h) * seq_len * seq_len + q_idx * seq_len;
    
    for (int k_idx = 0; k_idx < seq_len; k_idx++) {
        const float* k_row = K + qk_offset + k_idx * head_dim;
        float dot = 0.0f;
        for (int d = 0; d < head_dim; d++) {
            dot += q_row[d] * k_row[d];
        }
        score_row[k_idx] = dot * scale;
    }
}

/**
 * Apply attention weights to values: softmax(scores) * V
 */
__global__ void ml_attention_output_kernel(
    const float* __restrict__ scores,
    const float* __restrict__ V,
    float* __restrict__ output,
    int batch_size,
    int num_heads,
    int seq_len,
    int head_dim
) {
    int b = blockIdx.z;
    int h = blockIdx.y;
    int q_idx = blockIdx.x * blockDim.x + threadIdx.x;
    
    if (b >= batch_size || h >= num_heads || q_idx >= seq_len) return;
    
    int scores_offset = (b * num_heads + h) * seq_len * seq_len + q_idx * seq_len;
    int v_offset = (b * num_heads + h) * seq_len * head_dim;
    int out_offset = v_offset + q_idx * head_dim;
    
    for (int d = 0; d < head_dim; d++) {
        float sum = 0.0f;
        for (int k_idx = 0; k_idx < seq_len; k_idx++) {
            sum += scores[scores_offset + k_idx] * V[v_offset + k_idx * head_dim + d];
        }
        output[out_offset + d] = sum;
    }
}

/* Host wrapper functions */

extern "C" {

cudaError_t kolibri_ml_softmax(
    const float* input,
    float* output,
    int batch_size,
    int seq_len,
    int hidden_size,
    cudaStream_t stream
) {
    int total_rows = batch_size * seq_len;
    int blocks = (total_rows + BLOCK_SIZE - 1) / BLOCK_SIZE;
    
    ml_softmax_kernel<<<blocks, BLOCK_SIZE, 0, stream>>>(
        input, output, batch_size, seq_len, hidden_size
    );
    
    return cudaGetLastError();
}

cudaError_t kolibri_ml_gelu(
    const float* input,
    float* output,
    int size,
    cudaStream_t stream
) {
    int blocks = (size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    ml_gelu_kernel<<<blocks, BLOCK_SIZE, 0, stream>>>(input, output, size);
    return cudaGetLastError();
}

cudaError_t kolibri_ml_layer_norm(
    const float* input,
    const float* gamma,
    const float* beta,
    float* output,
    int batch_size,
    int hidden_size,
    float eps,
    cudaStream_t stream
) {
    ml_layer_norm_kernel<<<batch_size, BLOCK_SIZE, 0, stream>>>(
        input, gamma, beta, output, batch_size, hidden_size, eps
    );
    return cudaGetLastError();
}

cudaError_t kolibri_ml_matmul(
    const float* A,
    const float* B,
    float* C,
    int M, int K, int N,
    cudaStream_t stream
) {
    dim3 block(TILE_SIZE, TILE_SIZE);
    dim3 grid((N + TILE_SIZE - 1) / TILE_SIZE, (M + TILE_SIZE - 1) / TILE_SIZE);
    
    ml_matmul_tiled_kernel<<<grid, block, 0, stream>>>(A, B, C, M, K, N);
    return cudaGetLastError();
}

} /* extern "C" */
