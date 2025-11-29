"""Device detection and management for Kolibri ML system."""

from __future__ import annotations

import platform
import sys
from dataclasses import dataclass
from enum import Enum
from typing import List, Optional


class DeviceType(Enum):
    """Enumeration of supported device types."""

    CPU = "cpu"
    CUDA = "cuda"
    METAL = "metal"
    WASM = "wasm"


@dataclass
class DeviceInfo:
    """Information about a compute device."""

    device_type: DeviceType
    device_id: int = 0
    name: str = ""
    memory_total: int = 0  # bytes
    memory_available: int = 0  # bytes
    compute_capability: Optional[str] = None
    is_available: bool = True

    @property
    def device_string(self) -> str:
        """Return device string for PyTorch/ONNX compatibility."""
        if self.device_type == DeviceType.CPU:
            return "cpu"
        elif self.device_type == DeviceType.CUDA:
            return f"cuda:{self.device_id}"
        elif self.device_type == DeviceType.METAL:
            return "mps"
        elif self.device_type == DeviceType.WASM:
            return "wasm"
        return "cpu"


def _detect_cuda() -> Optional[DeviceInfo]:
    """Detect CUDA GPU availability."""
    try:
        import torch

        if torch.cuda.is_available():
            device_id = 0
            props = torch.cuda.get_device_properties(device_id)
            return DeviceInfo(
                device_type=DeviceType.CUDA,
                device_id=device_id,
                name=props.name,
                memory_total=props.total_memory,
                memory_available=torch.cuda.mem_get_info(device_id)[0],
                compute_capability=f"{props.major}.{props.minor}",
                is_available=True,
            )
    except ImportError:
        pass
    except Exception:
        pass
    return None


def _detect_metal() -> Optional[DeviceInfo]:
    """Detect Apple Metal GPU availability."""
    if platform.system() != "Darwin":
        return None

    try:
        import torch

        if hasattr(torch.backends, "mps") and torch.backends.mps.is_available():
            return DeviceInfo(
                device_type=DeviceType.METAL,
                device_id=0,
                name="Apple Silicon GPU",
                is_available=True,
            )
    except ImportError:
        pass
    except Exception:
        pass
    return None


def _detect_wasm() -> Optional[DeviceInfo]:
    """Detect WebAssembly environment."""
    if "emscripten" in sys.platform or "wasi" in sys.platform:
        return DeviceInfo(
            device_type=DeviceType.WASM,
            device_id=0,
            name="WebAssembly",
            is_available=True,
        )
    return None


def _get_cpu_info() -> DeviceInfo:
    """Get CPU device information."""
    import os

    cpu_count = os.cpu_count() or 1
    return DeviceInfo(
        device_type=DeviceType.CPU,
        device_id=0,
        name=f"{platform.processor() or 'CPU'} ({cpu_count} cores)",
        is_available=True,
    )


def detect_all_devices() -> List[DeviceInfo]:
    """Detect all available compute devices.

    Returns:
        List of available devices, ordered by preference (GPU first, CPU last).
    """
    devices: List[DeviceInfo] = []

    # Check GPU devices first
    cuda_device = _detect_cuda()
    if cuda_device:
        devices.append(cuda_device)

    metal_device = _detect_metal()
    if metal_device:
        devices.append(metal_device)

    # Check WASM
    wasm_device = _detect_wasm()
    if wasm_device:
        devices.append(wasm_device)

    # CPU is always available as fallback
    devices.append(_get_cpu_info())

    return devices


def get_device(preference: str = "auto") -> DeviceInfo:
    """Get the best available device based on preference.

    Args:
        preference: Device preference - "auto", "cpu", "cuda", "metal", or "wasm".

    Returns:
        DeviceInfo for the selected device.
    """
    all_devices = detect_all_devices()

    if preference == "auto":
        # Return first available device (already ordered by preference)
        return all_devices[0]

    # Try to find requested device type
    preference_map = {
        "cpu": DeviceType.CPU,
        "cuda": DeviceType.CUDA,
        "metal": DeviceType.METAL,
        "wasm": DeviceType.WASM,
    }

    requested_type = preference_map.get(preference.lower())
    if requested_type:
        for device in all_devices:
            if device.device_type == requested_type and device.is_available:
                return device

    # Fallback to CPU
    return _get_cpu_info()
