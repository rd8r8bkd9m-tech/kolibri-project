"""Tests for ML training infrastructure."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

import numpy as np  # noqa: E402
import pytest  # noqa: E402

from ml.models.classifier import Classifier  # noqa: E402
from ml.training.trainer import Trainer, TrainingConfig  # noqa: E402
from ml.training.data_loader import DataLoader, ArrayDataset, BatchSampler  # noqa: E402
from ml.training.metrics import compute_loss, compute_accuracy, MetricTracker  # noqa: E402
from ml.training.callbacks import LoggingCallback, EarlyStoppingCallback  # noqa: E402


class TestDataLoader:
    """Tests for data loading utilities."""

    def test_array_dataset(self) -> None:
        """Test ArrayDataset."""
        inputs = np.random.randn(100, 32).astype(np.float32)
        targets = np.random.randint(0, 10, 100)
        
        dataset = ArrayDataset(inputs, targets)
        assert len(dataset) == 100
        
        x, y = dataset[0]
        assert x.shape == (32,)

    def test_batch_sampler(self) -> None:
        """Test BatchSampler."""
        inputs = np.random.randn(100, 32).astype(np.float32)
        targets = np.random.randint(0, 10, 100)
        dataset = ArrayDataset(inputs, targets)
        
        sampler = BatchSampler(dataset, batch_size=16, shuffle=False)
        batches = list(sampler)
        
        assert len(batches) == 7  # ceil(100/16)
        assert len(batches[0]) == 16
        assert len(batches[-1]) == 4

    def test_dataloader_iteration(self) -> None:
        """Test DataLoader iteration."""
        inputs = np.random.randn(50, 16).astype(np.float32)
        targets = np.random.randint(0, 5, 50)
        dataset = ArrayDataset(inputs, targets)
        
        loader = DataLoader(dataset, batch_size=10)
        
        total_samples = 0
        for batch_x, batch_y in loader:
            total_samples += len(batch_x)
            assert batch_x.shape[1] == 16
        
        assert total_samples == 50

    def test_dataloader_shuffle(self) -> None:
        """Test DataLoader shuffling."""
        inputs = np.arange(100).reshape(100, 1).astype(np.float32)
        targets = np.arange(100)
        dataset = ArrayDataset(inputs, targets)
        
        loader1 = DataLoader(dataset, batch_size=10, shuffle=True, seed=42)
        loader2 = DataLoader(dataset, batch_size=10, shuffle=True, seed=42)
        
        batches1 = list(loader1)
        batches2 = list(loader2)
        
        # Same seed should produce same batches
        np.testing.assert_array_equal(batches1[0][0], batches2[0][0])


class TestMetrics:
    """Tests for metrics computation."""

    def test_cross_entropy_loss(self) -> None:
        """Test cross-entropy loss computation."""
        # Perfect predictions
        logits = np.array([[10, 0, 0], [0, 10, 0]], dtype=np.float32)
        targets = np.array([0, 1])
        
        loss = compute_loss(logits, targets, "cross_entropy")
        assert loss < 0.1  # Should be very low

    def test_mse_loss(self) -> None:
        """Test MSE loss computation."""
        predictions = np.array([1.0, 2.0, 3.0])
        targets = np.array([1.0, 2.0, 3.0])
        
        loss = compute_loss(predictions, targets, "mse")
        assert loss == 0.0

    def test_accuracy(self) -> None:
        """Test accuracy computation."""
        predictions = np.array([[0.9, 0.1], [0.2, 0.8], [0.6, 0.4]])
        targets = np.array([0, 1, 0])
        
        acc = compute_accuracy(predictions, targets)
        assert acc == 1.0

    def test_metric_tracker(self) -> None:
        """Test MetricTracker."""
        tracker = MetricTracker(window_size=5)
        
        for i in range(10):
            tracker.update({"loss": float(i)})
        
        avg = tracker.get_average("loss")
        assert avg == 7.0  # Average of [5,6,7,8,9]
        
        latest = tracker.get_latest("loss")
        assert latest == 9.0


class TestCallbacks:
    """Tests for training callbacks."""

    def test_logging_callback(self) -> None:
        """Test LoggingCallback."""
        callback = LoggingCallback(log_every=5)
        callback.on_train_start()
        callback.on_epoch_start(0)
        callback.on_batch_end(5, {"loss": 0.5})
        callback.on_epoch_end(0, {"loss": 0.5})
        callback.on_train_end()

    def test_early_stopping(self) -> None:
        """Test EarlyStoppingCallback."""
        callback = EarlyStoppingCallback(monitor="loss", patience=3)
        
        # Improving loss
        callback.on_epoch_end(0, {"loss": 1.0})
        assert not callback.should_stop
        
        callback.on_epoch_end(1, {"loss": 0.8})
        assert not callback.should_stop
        
        # No improvement
        callback.on_epoch_end(2, {"loss": 0.9})
        callback.on_epoch_end(3, {"loss": 0.85})
        callback.on_epoch_end(4, {"loss": 0.88})
        
        assert callback.should_stop


class TestTrainer:
    """Tests for Trainer."""

    def test_trainer_init(self) -> None:
        """Test Trainer initialization."""
        model = Classifier(input_dim=32, num_classes=5)
        config = TrainingConfig(epochs=1, batch_size=8)
        
        trainer = Trainer(model, config)
        assert trainer.global_step == 0
        assert trainer.epoch == 0

    def test_evaluate(self) -> None:
        """Test model evaluation."""
        model = Classifier(input_dim=16, num_classes=3)
        config = TrainingConfig(epochs=1, batch_size=4)
        trainer = Trainer(model, config)
        
        # Create simple eval data
        inputs = np.random.randn(10, 16).astype(np.float32)
        targets = np.random.randint(0, 3, 10)
        dataset = ArrayDataset(inputs, targets)
        loader = DataLoader(dataset, batch_size=4, shuffle=False)
        eval_data = loader.get_batches()
        
        metrics = trainer.evaluate(eval_data)
        assert "loss" in metrics


class TestIntegration:
    """Integration tests for training pipeline."""

    def test_full_training_pipeline(self) -> None:
        """Test complete training pipeline."""
        # Create model
        model = Classifier(input_dim=8, hidden_dims=[4], num_classes=2)
        
        # Create data
        np.random.seed(42)
        inputs = np.random.randn(50, 8).astype(np.float32)
        targets = (inputs[:, 0] > 0).astype(np.int64)  # Simple rule
        
        dataset = ArrayDataset(inputs, targets)
        loader = DataLoader(dataset, batch_size=10, shuffle=True)
        train_data = loader.get_batches()
        
        # Create trainer
        config = TrainingConfig(
            epochs=1,
            batch_size=10,
            learning_rate=0.01,
        )
        trainer = Trainer(model, config)
        
        # Train
        history = trainer.train(train_data)
        
        assert "train_loss" in history
        assert len(history["train_loss"]) == 1


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
