"""GPU backend integration for ML inference acceleration."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, List, Optional

import numpy as np

from ..utils.device_detector import DeviceType, get_device, detect_all_devices
from ..utils.logger import get_logger


@dataclass
class GPUInfo:
    """GPU device information."""

    device_id: int
    name: str
    vendor: str
    memory_total: int
    memory_free: int
    compute_capability: Optional[str] = None
    is_available: bool = True


def detect_gpu() -> List[GPUInfo]:
    """Detect available GPU devices.

    Returns:
        List of available GPU devices.
    """
    logger = get_logger()
    gpus: List[GPUInfo] = []

    # Check CUDA
    try:
        import torch

        if torch.cuda.is_available():
            for i in range(torch.cuda.device_count()):
                props = torch.cuda.get_device_properties(i)
                free_mem, total_mem = torch.cuda.mem_get_info(i)
                gpus.append(GPUInfo(
                    device_id=i,
                    name=props.name,
                    vendor="NVIDIA",
                    memory_total=total_mem,
                    memory_free=free_mem,
                    compute_capability=f"{props.major}.{props.minor}",
                    is_available=True,
                ))
                logger.debug(f"Found CUDA GPU: {props.name}")
    except ImportError:
        pass
    except Exception as e:
        logger.debug(f"CUDA detection failed: {e}")

    # Check Metal (Apple Silicon)
    try:
        import torch

        if hasattr(torch.backends, "mps") and torch.backends.mps.is_available():
            gpus.append(GPUInfo(
                device_id=0,
                name="Apple Silicon GPU",
                vendor="Apple",
                memory_total=0,  # Not easily queryable
                memory_free=0,
                is_available=True,
            ))
            logger.debug("Found Apple Metal GPU")
    except ImportError:
        pass
    except Exception as e:
        logger.debug(f"Metal detection failed: {e}")

    return gpus


class GPUBackend:
    """Backend for GPU-accelerated ML operations.

    Provides unified interface for:
    - CUDA kernels
    - Metal kernels
    - CPU fallback
    """

    def __init__(
        self,
        device: str = "auto",
        memory_limit: Optional[int] = None,
    ) -> None:
        """Initialize GPU backend.

        Args:
            device: Device preference ("auto", "cuda", "metal", "cpu").
            memory_limit: Maximum GPU memory to use (bytes).
        """
        self.logger = get_logger()
        self.memory_limit = memory_limit

        # Detect best device
        device_info = get_device(device)
        self.device_type = device_info.device_type
        self.device_string = device_info.device_string

        self.logger.info(f"GPU Backend initialized: {self.device_string}")

    def matmul(self, a: np.ndarray, b: np.ndarray) -> np.ndarray:
        """Matrix multiplication with GPU acceleration.

        Args:
            a: First matrix.
            b: Second matrix.

        Returns:
            Result matrix.
        """
        if self.device_type == DeviceType.CUDA:
            return self._cuda_matmul(a, b)
        elif self.device_type == DeviceType.METAL:
            return self._metal_matmul(a, b)
        return np.matmul(a, b)

    def _cuda_matmul(self, a: np.ndarray, b: np.ndarray) -> np.ndarray:
        """CUDA-accelerated matrix multiplication."""
        try:
            import torch

            device = torch.device("cuda")
            a_t = torch.from_numpy(a).to(device)
            b_t = torch.from_numpy(b).to(device)
            result = torch.matmul(a_t, b_t)
            return result.cpu().numpy()
        except Exception:
            return np.matmul(a, b)

    def _metal_matmul(self, a: np.ndarray, b: np.ndarray) -> np.ndarray:
        """Metal-accelerated matrix multiplication."""
        try:
            import torch

            device = torch.device("mps")
            a_t = torch.from_numpy(a).to(device)
            b_t = torch.from_numpy(b).to(device)
            result = torch.matmul(a_t, b_t)
            return result.cpu().numpy()
        except Exception:
            return np.matmul(a, b)

    def softmax(self, x: np.ndarray, axis: int = -1) -> np.ndarray:
        """Softmax with GPU acceleration.

        Args:
            x: Input array.
            axis: Axis to apply softmax.

        Returns:
            Softmax result.
        """
        if self.device_type in (DeviceType.CUDA, DeviceType.METAL):
            return self._gpu_softmax(x, axis)
        return self._cpu_softmax(x, axis)

    def _gpu_softmax(self, x: np.ndarray, axis: int) -> np.ndarray:
        """GPU-accelerated softmax."""
        try:
            import torch
            import torch.nn.functional as F

            device = "cuda" if self.device_type == DeviceType.CUDA else "mps"
            x_t = torch.from_numpy(x).to(device)
            result = F.softmax(x_t, dim=axis)
            return result.cpu().numpy()
        except Exception:
            return self._cpu_softmax(x, axis)

    def _cpu_softmax(self, x: np.ndarray, axis: int) -> np.ndarray:
        """CPU softmax implementation."""
        x_max = np.max(x, axis=axis, keepdims=True)
        exp_x = np.exp(x - x_max)
        return exp_x / np.sum(exp_x, axis=axis, keepdims=True)

    def batch_inference(
        self,
        model_fn: Any,
        inputs: List[np.ndarray],
        batch_size: int = 32,
    ) -> List[np.ndarray]:
        """Batch inference with GPU optimization.

        Args:
            model_fn: Model forward function.
            inputs: List of input arrays.
            batch_size: Batch size for processing.

        Returns:
            List of output arrays.
        """
        results: List[np.ndarray] = []

        for i in range(0, len(inputs), batch_size):
            batch = inputs[i : i + batch_size]
            batch_array = np.stack(batch)
            outputs = model_fn(batch_array)
            results.extend([outputs[j] for j in range(len(batch))])

        return results

    def get_status(self) -> Dict[str, Any]:
        """Get GPU backend status.

        Returns:
            Status dictionary.
        """
        status = {
            "device_type": self.device_type.value,
            "device_string": self.device_string,
            "available_gpus": len(detect_gpu()),
        }

        if self.device_type == DeviceType.CUDA:
            try:
                import torch

                status["cuda_version"] = torch.version.cuda
                status["memory_allocated"] = torch.cuda.memory_allocated()
                status["memory_reserved"] = torch.cuda.memory_reserved()
            except Exception:
                pass

        return status


def generate_cuda_kernels(output_path: str) -> None:
    """Generate CUDA kernel templates for ML operations.

    Args:
        output_path: Output file path.
    """
    cuda_code = '''/**
 * Kolibri ML CUDA Kernels
 * Auto-generated template
 */

#include <cuda_runtime.h>

// Softmax kernel
__global__ void softmax_kernel(
    const float* input,
    float* output,
    int batch_size,
    int seq_len,
    int hidden_size
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= batch_size * seq_len) return;

    const float* row = input + idx * hidden_size;
    float* out_row = output + idx * hidden_size;

    // Find max
    float max_val = row[0];
    for (int i = 1; i < hidden_size; i++) {
        max_val = fmaxf(max_val, row[i]);
    }

    // Compute exp and sum
    float sum = 0.0f;
    for (int i = 0; i < hidden_size; i++) {
        out_row[i] = expf(row[i] - max_val);
        sum += out_row[i];
    }

    // Normalize
    for (int i = 0; i < hidden_size; i++) {
        out_row[i] /= sum;
    }
}

// Layer normalization kernel
__global__ void layer_norm_kernel(
    const float* input,
    const float* gamma,
    const float* beta,
    float* output,
    int batch_size,
    int hidden_size,
    float eps
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= batch_size) return;

    const float* row = input + idx * hidden_size;
    float* out_row = output + idx * hidden_size;

    // Compute mean
    float mean = 0.0f;
    for (int i = 0; i < hidden_size; i++) {
        mean += row[i];
    }
    mean /= hidden_size;

    // Compute variance
    float var = 0.0f;
    for (int i = 0; i < hidden_size; i++) {
        float diff = row[i] - mean;
        var += diff * diff;
    }
    var /= hidden_size;

    // Normalize
    float inv_std = rsqrtf(var + eps);
    for (int i = 0; i < hidden_size; i++) {
        out_row[i] = gamma[i] * (row[i] - mean) * inv_std + beta[i];
    }
}
'''

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(cuda_code)


def generate_metal_kernels(output_path: str) -> None:
    """Generate Metal kernel templates for ML operations.

    Args:
        output_path: Output file path.
    """
    metal_code = '''/**
 * Kolibri ML Metal Kernels
 * Auto-generated template
 */

#include <metal_stdlib>
using namespace metal;

// Softmax kernel
kernel void softmax_kernel(
    const device float* input [[buffer(0)]],
    device float* output [[buffer(1)]],
    constant uint& batch_size [[buffer(2)]],
    constant uint& seq_len [[buffer(3)]],
    constant uint& hidden_size [[buffer(4)]],
    uint idx [[thread_position_in_grid]]
) {
    if (idx >= batch_size * seq_len) return;

    const device float* row = input + idx * hidden_size;
    device float* out_row = output + idx * hidden_size;

    // Find max
    float max_val = row[0];
    for (uint i = 1; i < hidden_size; i++) {
        max_val = max(max_val, row[i]);
    }

    // Compute exp and sum
    float sum = 0.0f;
    for (uint i = 0; i < hidden_size; i++) {
        out_row[i] = exp(row[i] - max_val);
        sum += out_row[i];
    }

    // Normalize
    for (uint i = 0; i < hidden_size; i++) {
        out_row[i] /= sum;
    }
}

// GELU activation kernel
kernel void gelu_kernel(
    const device float* input [[buffer(0)]],
    device float* output [[buffer(1)]],
    uint idx [[thread_position_in_grid]]
) {
    float x = input[idx];
    float cdf = 0.5f * (1.0f + tanh(sqrt(2.0f / M_PI_F) * (x + 0.044715f * x * x * x)));
    output[idx] = x * cdf;
}
'''

    with open(output_path, "w", encoding="utf-8") as f:
        f.write(metal_code)
