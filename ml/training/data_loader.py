"""Data loading utilities for training."""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Any, Generator, Iterator, List, Optional, Tuple

import numpy as np


class Dataset(ABC):
    """Abstract base class for datasets."""

    @abstractmethod
    def __len__(self) -> int:
        """Return dataset size."""
        pass

    @abstractmethod
    def __getitem__(self, idx: int) -> Tuple[np.ndarray, np.ndarray]:
        """Get single item."""
        pass


class ArrayDataset(Dataset):
    """Dataset from numpy arrays."""

    def __init__(self, inputs: np.ndarray, targets: np.ndarray) -> None:
        """Initialize dataset.

        Args:
            inputs: Input array [N, ...].
            targets: Target array [N, ...].
        """
        if len(inputs) != len(targets):
            raise ValueError("Inputs and targets must have same length")
        self.inputs = inputs
        self.targets = targets

    def __len__(self) -> int:
        return len(self.inputs)

    def __getitem__(self, idx: int) -> Tuple[np.ndarray, np.ndarray]:
        return self.inputs[idx], self.targets[idx]


class StreamingDataset(Dataset):
    """Dataset that streams data from a generator."""

    def __init__(
        self,
        generator_fn: Any,
        size: int = -1,
        buffer_size: int = 1000,
    ) -> None:
        """Initialize streaming dataset.

        Args:
            generator_fn: Function that returns a generator of (input, target) tuples.
            size: Known size of dataset (-1 if unknown).
            buffer_size: Size of internal buffer.
        """
        self.generator_fn = generator_fn
        self._size = size
        self.buffer_size = buffer_size
        self._buffer: List[Tuple[np.ndarray, np.ndarray]] = []
        self._generator: Optional[Iterator[Tuple[np.ndarray, np.ndarray]]] = None
        self._exhausted = False

    def _fill_buffer(self) -> None:
        """Fill internal buffer from generator."""
        if self._generator is None:
            self._generator = iter(self.generator_fn())

        while len(self._buffer) < self.buffer_size:
            try:
                item = next(self._generator)
                self._buffer.append(item)
            except StopIteration:
                self._exhausted = True
                break

    def __len__(self) -> int:
        if self._size >= 0:
            return self._size
        return len(self._buffer)

    def __getitem__(self, idx: int) -> Tuple[np.ndarray, np.ndarray]:
        while idx >= len(self._buffer) and not self._exhausted:
            self._fill_buffer()
        if idx >= len(self._buffer):
            raise IndexError(f"Index {idx} out of range")
        return self._buffer[idx]

    def reset(self) -> None:
        """Reset the generator for a new epoch."""
        self._generator = None
        self._buffer.clear()
        self._exhausted = False


class BatchSampler:
    """Batches samples from a dataset."""

    def __init__(
        self,
        dataset: Dataset,
        batch_size: int = 32,
        shuffle: bool = True,
        drop_last: bool = False,
        seed: Optional[int] = None,
    ) -> None:
        """Initialize batch sampler.

        Args:
            dataset: Source dataset.
            batch_size: Number of samples per batch.
            shuffle: Whether to shuffle indices.
            drop_last: Whether to drop the last incomplete batch.
            seed: Random seed for shuffling.
        """
        self.dataset = dataset
        self.batch_size = batch_size
        self.shuffle = shuffle
        self.drop_last = drop_last
        self.rng = np.random.default_rng(seed)

    def __iter__(self) -> Generator[List[int], None, None]:
        """Iterate over batch indices."""
        indices = list(range(len(self.dataset)))
        if self.shuffle:
            self.rng.shuffle(indices)

        batch: List[int] = []
        for idx in indices:
            batch.append(idx)
            if len(batch) == self.batch_size:
                yield batch
                batch = []

        if batch and not self.drop_last:
            yield batch

    def __len__(self) -> int:
        """Return number of batches."""
        n = len(self.dataset)
        if self.drop_last:
            return n // self.batch_size
        return (n + self.batch_size - 1) // self.batch_size


class DataLoader:
    """Data loader with batching and shuffling support."""

    def __init__(
        self,
        dataset: Dataset,
        batch_size: int = 32,
        shuffle: bool = True,
        drop_last: bool = False,
        num_workers: int = 0,
        seed: Optional[int] = None,
    ) -> None:
        """Initialize data loader.

        Args:
            dataset: Source dataset.
            batch_size: Number of samples per batch.
            shuffle: Whether to shuffle data.
            drop_last: Whether to drop last incomplete batch.
            num_workers: Number of worker processes (0 = main thread).
            seed: Random seed.
        """
        self.dataset = dataset
        self.batch_size = batch_size
        self.shuffle = shuffle
        self.drop_last = drop_last
        self.num_workers = num_workers
        self.seed = seed
        self._sampler = BatchSampler(dataset, batch_size, shuffle, drop_last, seed)

    def __iter__(self) -> Generator[Tuple[np.ndarray, np.ndarray], None, None]:
        """Iterate over batches."""
        for batch_indices in self._sampler:
            inputs = []
            targets = []
            for idx in batch_indices:
                inp, tgt = self.dataset[idx]
                inputs.append(inp)
                targets.append(tgt)
            yield np.stack(inputs), np.stack(targets)

    def __len__(self) -> int:
        """Return number of batches."""
        return len(self._sampler)

    def get_batches(self) -> List[Tuple[np.ndarray, np.ndarray]]:
        """Return all batches as a list."""
        return list(self)


def collate_fn(
    batch: List[Tuple[np.ndarray, np.ndarray]],
    padding_value: int = 0,
) -> Tuple[np.ndarray, np.ndarray]:
    """Collate function with padding for variable-length sequences.

    Args:
        batch: List of (input, target) tuples.
        padding_value: Value used for padding.

    Returns:
        Tuple of padded (inputs, targets) arrays.
    """
    inputs = [item[0] for item in batch]
    targets = [item[1] for item in batch]

    # Find max lengths
    max_input_len = max(len(inp) for inp in inputs)
    max_target_len = max(len(tgt) for tgt in targets)

    # Pad inputs
    padded_inputs = np.full(
        (len(inputs), max_input_len),
        padding_value,
        dtype=inputs[0].dtype,
    )
    for i, inp in enumerate(inputs):
        padded_inputs[i, : len(inp)] = inp

    # Pad targets
    padded_targets = np.full(
        (len(targets), max_target_len),
        padding_value,
        dtype=targets[0].dtype,
    )
    for i, tgt in enumerate(targets):
        padded_targets[i, : len(tgt)] = tgt

    return padded_inputs, padded_targets
