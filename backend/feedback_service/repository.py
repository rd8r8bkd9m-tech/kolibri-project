"""Repository coordinating feedback persistence and dataset export."""

from __future__ import annotations

from typing import Annotated

from fastapi import Depends

from .database import FeedbackStorage, FeedbackStorageError, get_feedback_storage
from .rlhf_dataset import RLHFDatasetWriter, get_dataset_writer
from .schemas import FeedbackPayload, FeedbackRecord


class FeedbackRepository:
    """High level orchestrator combining database storage and RLHF export."""

    def __init__(
        self, storage: FeedbackStorage, dataset_writer: RLHFDatasetWriter
    ) -> None:
        self._storage = storage
        self._dataset_writer = dataset_writer

    async def create_feedback(self, payload: FeedbackPayload) -> FeedbackRecord:
        """Persist the payload and mirror it into the RLHF dataset."""

        record = await self._storage.save_feedback(payload)
        await self._dataset_writer.append(record)
        return record


async def get_repository(
    storage: Annotated[FeedbackStorage, Depends(get_feedback_storage)],
    dataset_writer: Annotated[RLHFDatasetWriter, Depends(get_dataset_writer)],
) -> FeedbackRepository:
    """FastAPI dependency constructing a feedback repository instance."""

    return FeedbackRepository(storage, dataset_writer)


__all__ = ["FeedbackRepository", "FeedbackStorageError", "get_repository"]
