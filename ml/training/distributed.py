"""Distributed training support for multi-GPU and multi-node training."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Tuple

import numpy as np

from ..models.base_model import BaseModel
from ..utils.logger import get_logger
from .trainer import Trainer, TrainingConfig


@dataclass
class DistributedConfig:
    """Configuration for distributed training."""

    world_size: int = 1
    rank: int = 0
    local_rank: int = 0
    backend: str = "gloo"  # "gloo", "nccl", or "mpi"
    master_addr: str = "localhost"
    master_port: int = 29500


class DistributedTrainer:
    """Distributed trainer for multi-GPU/multi-node training.

    Supports:
    - Data parallelism
    - Gradient synchronization
    - Checkpointing across ranks
    """

    def __init__(
        self,
        model: BaseModel,
        training_config: TrainingConfig,
        distributed_config: DistributedConfig,
    ) -> None:
        """Initialize distributed trainer.

        Args:
            model: Model to train.
            training_config: Training configuration.
            distributed_config: Distributed configuration.
        """
        self.model = model
        self.training_config = training_config
        self.distributed_config = distributed_config
        self.logger = get_logger()

        self._initialized = False
        self._trainer: Optional[Trainer] = None

    def setup(self) -> None:
        """Initialize distributed training environment."""
        if self._initialized:
            return

        world_size = self.distributed_config.world_size
        rank = self.distributed_config.rank

        self.logger.info(f"Initializing distributed training: rank {rank}/{world_size}")

        # In production, this would initialize process groups
        # For now, we simulate single-process training
        self._trainer = Trainer(self.model, self.training_config)
        self._initialized = True

    def cleanup(self) -> None:
        """Cleanup distributed training resources."""
        self._initialized = False

    def _partition_data(
        self,
        data: List[Tuple[np.ndarray, np.ndarray]],
    ) -> List[Tuple[np.ndarray, np.ndarray]]:
        """Partition data across ranks.

        Args:
            data: Full dataset.

        Returns:
            Partition for current rank.
        """
        world_size = self.distributed_config.world_size
        rank = self.distributed_config.rank

        # Simple partitioning
        partition_size = len(data) // world_size
        start_idx = rank * partition_size
        end_idx = start_idx + partition_size if rank < world_size - 1 else len(data)

        return data[start_idx:end_idx]

    def _all_reduce_gradients(
        self,
        gradients: Dict[str, np.ndarray],
    ) -> Dict[str, np.ndarray]:
        """Synchronize gradients across all ranks.

        Args:
            gradients: Local gradients.

        Returns:
            Averaged gradients.
        """
        # In production, this would use collective operations
        # For now, return gradients unchanged (single process)
        return gradients

    def train(
        self,
        train_data: List[Tuple[np.ndarray, np.ndarray]],
        eval_data: Optional[List[Tuple[np.ndarray, np.ndarray]]] = None,
    ) -> Dict[str, Any]:
        """Run distributed training.

        Args:
            train_data: Training data.
            eval_data: Optional validation data.

        Returns:
            Training history.
        """
        self.setup()

        if self._trainer is None:
            raise RuntimeError("Trainer not initialized")

        # Partition data
        local_train_data = self._partition_data(train_data)
        local_eval_data = self._partition_data(eval_data) if eval_data else None

        self.logger.info(
            f"Rank {self.distributed_config.rank}: "
            f"Training on {len(local_train_data)} samples"
        )

        # Train
        history = self._trainer.train(local_train_data, local_eval_data)

        # Synchronize final model (in production, would all-reduce parameters)
        return history

    def save_checkpoint(self, name: str = "checkpoint") -> None:
        """Save checkpoint (only on rank 0).

        Args:
            name: Checkpoint name.
        """
        if self.distributed_config.rank == 0 and self._trainer:
            self._trainer.save_checkpoint(name)

    def load_checkpoint(self, path: str) -> None:
        """Load checkpoint on all ranks.

        Args:
            path: Path to checkpoint.
        """
        if self._trainer:
            self._trainer.load_checkpoint(path)

    @property
    def is_main_process(self) -> bool:
        """Check if this is the main process (rank 0)."""
        return self.distributed_config.rank == 0
