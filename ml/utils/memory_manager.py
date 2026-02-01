"""Memory management utilities for Kolibri ML system."""

from __future__ import annotations

import gc
from dataclasses import dataclass
from typing import Optional


@dataclass
class MemoryStats:
    """Container for memory statistics."""

    allocated: int = 0  # bytes
    reserved: int = 0  # bytes
    peak: int = 0  # bytes
    device: str = "cpu"


class MemoryManager:
    """Manages memory allocation and cleanup for ML operations."""

    def __init__(self, device: str = "cpu", max_memory: Optional[int] = None) -> None:
        """Initialize memory manager.

        Args:
            device: Device type ("cpu", "cuda", "metal").
            max_memory: Maximum memory limit in bytes.
        """
        self.device = device
        self.max_memory = max_memory
        self._peak_usage: int = 0

    def get_stats(self) -> MemoryStats:
        """Get current memory statistics.

        Returns:
            MemoryStats with current memory usage.
        """
        if self.device.startswith("cuda"):
            return self._get_cuda_stats()
        elif self.device in ("mps", "metal"):
            return self._get_metal_stats()
        return self._get_cpu_stats()

    def _get_cuda_stats(self) -> MemoryStats:
        """Get CUDA memory statistics."""
        try:
            import torch

            if torch.cuda.is_available():
                allocated = torch.cuda.memory_allocated()
                reserved = torch.cuda.memory_reserved()
                peak = torch.cuda.max_memory_allocated()
                self._peak_usage = max(self._peak_usage, peak)
                return MemoryStats(
                    allocated=allocated,
                    reserved=reserved,
                    peak=peak,
                    device=self.device,
                )
        except ImportError:
            pass
        return MemoryStats(device=self.device)

    def _get_metal_stats(self) -> MemoryStats:
        """Get Metal (Apple Silicon) memory statistics."""
        try:
            import torch

            if hasattr(torch.mps, "current_allocated_memory"):
                allocated = torch.mps.current_allocated_memory()
                return MemoryStats(
                    allocated=allocated,
                    peak=self._peak_usage,
                    device=self.device,
                )
        except (ImportError, AttributeError):
            pass
        return MemoryStats(device=self.device)

    def _get_cpu_stats(self) -> MemoryStats:
        """Get CPU/system memory statistics."""
        try:
            import psutil

            process = psutil.Process()
            memory_info = process.memory_info()
            return MemoryStats(
                allocated=memory_info.rss,
                peak=self._peak_usage,
                device="cpu",
            )
        except ImportError:
            pass
        return MemoryStats(device="cpu")

    def clear_cache(self) -> None:
        """Clear GPU memory cache and run garbage collection."""
        gc.collect()

        if self.device.startswith("cuda"):
            try:
                import torch

                if torch.cuda.is_available():
                    torch.cuda.empty_cache()
                    torch.cuda.synchronize()
            except ImportError:
                pass

        elif self.device in ("mps", "metal"):
            try:
                import torch

                if hasattr(torch.mps, "empty_cache"):
                    torch.mps.empty_cache()
            except (ImportError, AttributeError):
                pass

    def check_allocation(self, size_bytes: int) -> bool:
        """Check if allocation of given size is possible.

        Args:
            size_bytes: Size to allocate in bytes.

        Returns:
            True if allocation should succeed.
        """
        if self.max_memory is None:
            return True

        stats = self.get_stats()
        return (stats.allocated + size_bytes) <= self.max_memory


def get_memory_stats(device: str = "cpu") -> MemoryStats:
    """Get memory statistics for specified device.

    Args:
        device: Device type.

    Returns:
        MemoryStats for the device.
    """
    manager = MemoryManager(device)
    return manager.get_stats()
