/**
 * Kolibri ML Metal Kernels
 * GPU-accelerated ML operations for Apple Silicon
 */

#include <metal_stdlib>
using namespace metal;

/* Constants */
constant float GELU_COEF = 0.7978845608f;
constant float GELU_CUBIC_COEF = 0.044715f;

/**
 * Softmax kernel - numerically stable implementation
 */
kernel void ml_softmax(
    const device float* input [[buffer(0)]],
    device float* output [[buffer(1)]],
    constant uint& batch_size [[buffer(2)]],
    constant uint& seq_len [[buffer(3)]],
    constant uint& hidden_size [[buffer(4)]],
    uint idx [[thread_position_in_grid]]
) {
    uint total_rows = batch_size * seq_len;
    if (idx >= total_rows) return;
    
    const device float* row = input + idx * hidden_size;
    device float* out_row = output + idx * hidden_size;
    
    /* Find maximum for numerical stability */
    float max_val = row[0];
    for (uint i = 1; i < hidden_size; i++) {
        max_val = max(max_val, row[i]);
    }
    
    /* Compute exp and sum */
    float sum = 0.0f;
    for (uint i = 0; i < hidden_size; i++) {
        out_row[i] = exp(row[i] - max_val);
        sum += out_row[i];
    }
    
    /* Normalize */
    float inv_sum = sum > 0.0f ? 1.0f / sum : 0.0f;
    for (uint i = 0; i < hidden_size; i++) {
        out_row[i] *= inv_sum;
    }
}

/**
 * GELU activation kernel
 * Approximation: 0.5 * x * (1 + tanh(sqrt(2/pi) * (x + 0.044715 * x^3)))
 */
kernel void ml_gelu(
    const device float* input [[buffer(0)]],
    device float* output [[buffer(1)]],
    uint idx [[thread_position_in_grid]]
) {
    float x = input[idx];
    float cdf = 0.5f * (1.0f + tanh(GELU_COEF * (x + GELU_CUBIC_COEF * x * x * x)));
    output[idx] = x * cdf;
}

/**
 * ReLU activation kernel
 */
kernel void ml_relu(
    const device float* input [[buffer(0)]],
    device float* output [[buffer(1)]],
    uint idx [[thread_position_in_grid]]
) {
    output[idx] = max(0.0f, input[idx]);
}

/**
 * Sigmoid activation kernel
 */
kernel void ml_sigmoid(
    const device float* input [[buffer(0)]],
    device float* output [[buffer(1)]],
    uint idx [[thread_position_in_grid]]
) {
    float x = input[idx];
    output[idx] = 1.0f / (1.0f + exp(-x));
}

/**
 * Tanh activation kernel
 */
kernel void ml_tanh(
    const device float* input [[buffer(0)]],
    device float* output [[buffer(1)]],
    uint idx [[thread_position_in_grid]]
) {
    output[idx] = tanh(input[idx]);
}

/**
 * Layer normalization kernel
 */
kernel void ml_layer_norm(
    const device float* input [[buffer(0)]],
    const device float* gamma [[buffer(1)]],
    const device float* beta [[buffer(2)]],
    device float* output [[buffer(3)]],
    constant uint& batch_size [[buffer(4)]],
    constant uint& hidden_size [[buffer(5)]],
    constant float& eps [[buffer(6)]],
    uint batch_idx [[thread_position_in_grid]]
) {
    if (batch_idx >= batch_size) return;
    
    const device float* row = input + batch_idx * hidden_size;
    device float* out_row = output + batch_idx * hidden_size;
    
    /* Compute mean */
    float mean = 0.0f;
    for (uint i = 0; i < hidden_size; i++) {
        mean += row[i];
    }
    mean /= float(hidden_size);
    
    /* Compute variance */
    float var = 0.0f;
    for (uint i = 0; i < hidden_size; i++) {
        float diff = row[i] - mean;
        var += diff * diff;
    }
    var /= float(hidden_size);
    
    /* Normalize and apply affine transform */
    float inv_std = rsqrt(var + eps);
    for (uint i = 0; i < hidden_size; i++) {
        float normalized = (row[i] - mean) * inv_std;
        out_row[i] = gamma[i] * normalized + beta[i];
    }
}

/**
 * Matrix-vector multiplication
 * y = A * x where A is [M, K] and x is [K]
 */
kernel void ml_matvec(
    const device float* A [[buffer(0)]],
    const device float* x [[buffer(1)]],
    device float* y [[buffer(2)]],
    constant uint& M [[buffer(3)]],
    constant uint& K [[buffer(4)]],
    uint row [[thread_position_in_grid]]
) {
    if (row >= M) return;
    
    const device float* a_row = A + row * K;
    float sum = 0.0f;
    for (uint k = 0; k < K; k++) {
        sum += a_row[k] * x[k];
    }
    y[row] = sum;
}

/**
 * Matrix multiplication with 2D thread groups
 * C = A * B where A is [M, K] and B is [K, N]
 */
kernel void ml_matmul(
    const device float* A [[buffer(0)]],
    const device float* B [[buffer(1)]],
    device float* C [[buffer(2)]],
    constant uint& M [[buffer(3)]],
    constant uint& K [[buffer(4)]],
    constant uint& N [[buffer(5)]],
    uint2 gid [[thread_position_in_grid]]
) {
    uint row = gid.y;
    uint col = gid.x;
    
    if (row >= M || col >= N) return;
    
    float sum = 0.0f;
    for (uint k = 0; k < K; k++) {
        sum += A[row * K + k] * B[k * N + col];
    }
    C[row * N + col] = sum;
}

/**
 * Elementwise addition
 */
kernel void ml_add(
    const device float* a [[buffer(0)]],
    const device float* b [[buffer(1)]],
    device float* c [[buffer(2)]],
    uint idx [[thread_position_in_grid]]
) {
    c[idx] = a[idx] + b[idx];
}

/**
 * Elementwise multiplication
 */
kernel void ml_mul(
    const device float* a [[buffer(0)]],
    const device float* b [[buffer(1)]],
    device float* c [[buffer(2)]],
    uint idx [[thread_position_in_grid]]
) {
    c[idx] = a[idx] * b[idx];
}

/**
 * Scale tensor by constant
 */
kernel void ml_scale(
    const device float* input [[buffer(0)]],
    device float* output [[buffer(1)]],
    constant float& scale [[buffer(2)]],
    uint idx [[thread_position_in_grid]]
) {
    output[idx] = input[idx] * scale;
}

/**
 * Attention score computation: Q * K^T / sqrt(d_k)
 */
kernel void ml_attention_scores(
    const device float* Q [[buffer(0)]],
    const device float* K [[buffer(1)]],
    device float* scores [[buffer(2)]],
    constant uint& batch_size [[buffer(3)]],
    constant uint& num_heads [[buffer(4)]],
    constant uint& seq_len [[buffer(5)]],
    constant uint& head_dim [[buffer(6)]],
    uint3 gid [[thread_position_in_grid]]
) {
    uint b = gid.z;
    uint h = gid.y;
    uint q_idx = gid.x;
    
    if (b >= batch_size || h >= num_heads || q_idx >= seq_len) return;
    
    float scale = rsqrt(float(head_dim));
    
    uint qk_offset = (b * num_heads + h) * seq_len * head_dim;
    const device float* q_row = Q + qk_offset + q_idx * head_dim;
    device float* score_row = scores + (b * num_heads + h) * seq_len * seq_len + q_idx * seq_len;
    
    for (uint k_idx = 0; k_idx < seq_len; k_idx++) {
        const device float* k_row = K + qk_offset + k_idx * head_dim;
        float dot = 0.0f;
        for (uint d = 0; d < head_dim; d++) {
            dot += q_row[d] * k_row[d];
        }
        score_row[k_idx] = dot * scale;
    }
}

/**
 * Embedding lookup kernel
 */
kernel void ml_embedding_lookup(
    const device float* embeddings [[buffer(0)]],
    const device uint* indices [[buffer(1)]],
    device float* output [[buffer(2)]],
    constant uint& embedding_dim [[buffer(3)]],
    uint2 gid [[thread_position_in_grid]]
) {
    uint seq_idx = gid.y;
    uint dim_idx = gid.x;
    
    if (dim_idx >= embedding_dim) return;
    
    uint token_id = indices[seq_idx];
    output[seq_idx * embedding_dim + dim_idx] = embeddings[token_id * embedding_dim + dim_idx];
}

/**
 * Position encoding addition
 */
kernel void ml_add_position_encoding(
    const device float* input [[buffer(0)]],
    const device float* pos_encoding [[buffer(1)]],
    device float* output [[buffer(2)]],
    constant uint& seq_len [[buffer(3)]],
    constant uint& hidden_size [[buffer(4)]],
    uint2 gid [[thread_position_in_grid]]
) {
    uint pos = gid.y;
    uint dim = gid.x;
    
    if (pos >= seq_len || dim >= hidden_size) return;
    
    uint idx = pos * hidden_size + dim;
    output[idx] = input[idx] + pos_encoding[idx];
}

/**
 * Reduce sum kernel
 */
kernel void ml_reduce_sum(
    const device float* input [[buffer(0)]],
    device float* output [[buffer(1)]],
    constant uint& size [[buffer(2)]],
    threadgroup float* shared [[threadgroup(0)]],
    uint tid [[thread_index_in_threadgroup]],
    uint gid [[threadgroup_position_in_grid]],
    uint group_size [[threads_per_threadgroup]]
) {
    /* Load into shared memory */
    shared[tid] = (tid < size) ? input[tid] : 0.0f;
    threadgroup_barrier(mem_flags::mem_threadgroup);
    
    /* Reduce in shared memory */
    for (uint s = group_size / 2; s > 0; s >>= 1) {
        if (tid < s) {
            shared[tid] += shared[tid + s];
        }
        threadgroup_barrier(mem_flags::mem_threadgroup);
    }
    
    /* Write result */
    if (tid == 0) {
        output[gid] = shared[0];
    }
}

/**
 * Cross entropy loss kernel
 */
kernel void ml_cross_entropy(
    const device float* logits [[buffer(0)]],
    const device uint* targets [[buffer(1)]],
    device float* losses [[buffer(2)]],
    constant uint& batch_size [[buffer(3)]],
    constant uint& num_classes [[buffer(4)]],
    uint idx [[thread_position_in_grid]]
) {
    if (idx >= batch_size) return;
    
    const device float* logit_row = logits + idx * num_classes;
    uint target = targets[idx];
    
    /* Compute log softmax */
    float max_val = logit_row[0];
    for (uint i = 1; i < num_classes; i++) {
        max_val = max(max_val, logit_row[i]);
    }
    
    float sum_exp = 0.0f;
    for (uint i = 0; i < num_classes; i++) {
        sum_exp += exp(logit_row[i] - max_val);
    }
    
    float log_softmax = logit_row[target] - max_val - log(sum_exp);
    losses[idx] = -log_softmax;
}
