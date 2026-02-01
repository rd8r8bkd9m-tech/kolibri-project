"""Integration bridges for Kolibri ML with existing components."""

from __future__ import annotations

from .kolibri_core_bridge import KolibriCoreBridge
from .archiver_ml import ArchiverML, NeuralEntropyEstimator
from .gpu_backend import GPUBackend, detect_gpu
from .swarm_ml import SwarmMLIntegration
from .cloud_ml import CloudMLSearch

__all__ = [
    "KolibriCoreBridge",
    "ArchiverML",
    "NeuralEntropyEstimator",
    "GPUBackend",
    "detect_gpu",
    "SwarmMLIntegration",
    "CloudMLSearch",
]
