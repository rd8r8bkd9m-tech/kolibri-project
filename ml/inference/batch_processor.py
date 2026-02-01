"""Batch processing utilities for efficient inference."""

from __future__ import annotations

import time
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from queue import Queue
from threading import Event, Thread
from typing import Any, Callable, Dict, Generic, List, Optional, TypeVar

import numpy as np

from ..utils.logger import get_logger

T = TypeVar("T")


@dataclass
class BatchRequest:
    """Request for batch processing."""

    inputs: np.ndarray
    request_id: str
    timestamp: float = 0.0

    def __post_init__(self) -> None:
        if self.timestamp == 0.0:
            self.timestamp = time.time()


@dataclass
class BatchResponse:
    """Response from batch processing."""

    outputs: np.ndarray
    request_id: str
    latency_ms: float


class BatchProcessor:
    """Processor for efficient batched inference.

    Features:
    - Dynamic batching
    - Request queuing
    - Timeout handling
    - Latency optimization
    """

    def __init__(
        self,
        process_fn: Callable[[np.ndarray], np.ndarray],
        max_batch_size: int = 32,
        max_wait_ms: float = 10.0,
        num_workers: int = 1,
    ) -> None:
        """Initialize batch processor.

        Args:
            process_fn: Function to process batched inputs.
            max_batch_size: Maximum batch size.
            max_wait_ms: Maximum wait time before processing.
            num_workers: Number of worker threads.
        """
        self.process_fn = process_fn
        self.max_batch_size = max_batch_size
        self.max_wait_ms = max_wait_ms
        self.num_workers = num_workers
        self.logger = get_logger()

        self._request_queue: Queue[BatchRequest] = Queue()
        self._response_queues: Dict[str, Queue[BatchResponse]] = {}
        self._running = Event()
        self._workers: List[Thread] = []

    def start(self) -> None:
        """Start batch processing workers."""
        if self._running.is_set():
            return

        self._running.set()
        for _ in range(self.num_workers):
            worker = Thread(target=self._worker_loop, daemon=True)
            worker.start()
            self._workers.append(worker)

        self.logger.info(f"Started {self.num_workers} batch processing workers")

    def stop(self) -> None:
        """Stop batch processing workers."""
        self._running.clear()
        for worker in self._workers:
            worker.join(timeout=1.0)
        self._workers.clear()
        self.logger.info("Stopped batch processing workers")

    def _worker_loop(self) -> None:
        """Worker loop for batch processing."""
        while self._running.is_set():
            batch = self._collect_batch()
            if batch:
                self._process_batch(batch)

    def _collect_batch(self) -> List[BatchRequest]:
        """Collect requests into a batch."""
        batch: List[BatchRequest] = []
        deadline = time.time() + self.max_wait_ms / 1000.0

        while len(batch) < self.max_batch_size:
            timeout = max(0, deadline - time.time())
            try:
                request = self._request_queue.get(timeout=timeout)
                batch.append(request)
            except Exception:
                break

            if time.time() >= deadline:
                break

        return batch

    def _process_batch(self, batch: List[BatchRequest]) -> None:
        """Process a batch of requests."""
        if not batch:
            return

        start_time = time.time()

        # Stack inputs
        inputs = np.stack([req.inputs for req in batch])

        # Process batch
        outputs = self.process_fn(inputs)

        # Distribute responses
        latency_ms = (time.time() - start_time) * 1000

        for i, request in enumerate(batch):
            response = BatchResponse(
                outputs=outputs[i] if outputs.ndim > 1 else outputs,
                request_id=request.request_id,
                latency_ms=latency_ms,
            )
            if request.request_id in self._response_queues:
                self._response_queues[request.request_id].put(response)

    def submit(
        self,
        inputs: np.ndarray,
        request_id: str,
        timeout: Optional[float] = None,
    ) -> Optional[BatchResponse]:
        """Submit request for batch processing.

        Args:
            inputs: Input data.
            request_id: Unique request identifier.
            timeout: Timeout in seconds.

        Returns:
            Response or None if timeout.
        """
        # Create response queue
        response_queue: Queue[BatchResponse] = Queue()
        self._response_queues[request_id] = response_queue

        # Submit request
        request = BatchRequest(inputs=inputs, request_id=request_id)
        self._request_queue.put(request)

        # Wait for response
        try:
            response = response_queue.get(timeout=timeout)
            return response
        except Exception:
            return None
        finally:
            del self._response_queues[request_id]

    def process_batch_sync(
        self,
        inputs_list: List[np.ndarray],
    ) -> List[np.ndarray]:
        """Synchronously process a list of inputs.

        Args:
            inputs_list: List of input arrays.

        Returns:
            List of output arrays.
        """
        if not inputs_list:
            return []

        results: List[np.ndarray] = []

        for i in range(0, len(inputs_list), self.max_batch_size):
            batch = inputs_list[i : i + self.max_batch_size]
            batch_array = np.stack(batch)
            outputs = self.process_fn(batch_array)
            results.extend([outputs[j] for j in range(len(batch))])

        return results


class AsyncBatchProcessor:
    """Asynchronous batch processor using thread pool."""

    def __init__(
        self,
        process_fn: Callable[[np.ndarray], np.ndarray],
        max_workers: int = 4,
    ) -> None:
        """Initialize async batch processor.

        Args:
            process_fn: Function to process inputs.
            max_workers: Maximum number of worker threads.
        """
        self.process_fn = process_fn
        self.executor = ThreadPoolExecutor(max_workers=max_workers)
        self.logger = get_logger()

    def submit(self, inputs: np.ndarray) -> Any:
        """Submit inputs for async processing.

        Args:
            inputs: Input data.

        Returns:
            Future object.
        """
        return self.executor.submit(self.process_fn, inputs)

    def map(
        self,
        inputs_list: List[np.ndarray],
        timeout: Optional[float] = None,
    ) -> List[np.ndarray]:
        """Process multiple inputs in parallel.

        Args:
            inputs_list: List of input arrays.
            timeout: Timeout in seconds.

        Returns:
            List of output arrays.
        """
        futures = [self.submit(inputs) for inputs in inputs_list]
        return [f.result(timeout=timeout) for f in futures]

    def shutdown(self, wait: bool = True) -> None:
        """Shutdown the thread pool.

        Args:
            wait: Whether to wait for pending tasks.
        """
        self.executor.shutdown(wait=wait)
