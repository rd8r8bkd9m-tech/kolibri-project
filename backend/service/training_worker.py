"""
training_worker.py — Отдельный процесс для evolve() + embedding training

Обходит GIL через multiprocessing: CPU-bound обучение не блокирует HTTP.

Архитектура:
  [FastAPI uvicorn] ←pipe→ [TrainingWorker process]
    HTTP-handler              evolve() в C / embedding SGD
    ~0ms блокировки           100% CPU утилизация

Протокол:
  Основной процесс → Worker: ("evolve", genomes, fitnesses, pairs, generations)
  Основной процесс → Worker: ("embeddings", vectors, edges, config)
  Worker → Основной: ("result", updated_data)
"""
from __future__ import annotations

import logging
import multiprocessing as mp
import os
import signal
import time
from multiprocessing import Process, Queue
from typing import Any, Optional

log = logging.getLogger("kolibri.training_worker")

# Размер очередей
_TASK_QUEUE_SIZE = 64
_RESULT_QUEUE_SIZE = 64


class TrainingWorker:
    """
    Отдельный процесс для CPU-bound обучения.

    Полностью обходит Python GIL:
    - evolve() формул → вызов C FFI или Python fallback
    - embedding training → вызов C FFI или Python fallback
    - Результаты возвращаются через mp.Queue
    """

    def __init__(self) -> None:
        self._task_queue: Queue = Queue(maxsize=_TASK_QUEUE_SIZE)
        self._result_queue: Queue = Queue(maxsize=_RESULT_QUEUE_SIZE)
        self._process: Optional[Process] = None
        self._running = mp.Value("b", False)

    def start(self) -> None:
        """Запустить worker-процесс."""
        if self._process and self._process.is_alive():
            return
        self._running.value = True
        self._process = Process(
            target=_worker_loop,
            args=(self._task_queue, self._result_queue, self._running),
            daemon=True,
            name="kolibri-training-worker",
        )
        self._process.start()
        log.info(
            "TrainingWorker запущен (PID=%d)", self._process.pid,
        )

    def stop(self) -> None:
        """Остановить worker."""
        self._running.value = False
        if self._process and self._process.is_alive():
            self._task_queue.put(("shutdown",))
            self._process.join(timeout=5)
            if self._process.is_alive():
                self._process.kill()
            log.info("TrainingWorker остановлен")

    @property
    def alive(self) -> bool:
        return self._process is not None and self._process.is_alive()

    def submit_evolve(
        self,
        genomes: list[list[int]],
        fitnesses: list[float],
        semantic_pairs: list[tuple[list[int], list[int]]],
        generations: int = 10,
        task_id: str = "",
    ) -> bool:
        """
        Отправить задачу эволюции в worker.
        Non-blocking: возвращает False если очередь полна.
        """
        try:
            self._task_queue.put_nowait((
                "evolve", task_id, genomes, fitnesses, semantic_pairs, generations,
            ))
            return True
        except Exception:
            return False

    def submit_embeddings(
        self,
        vectors: dict[int, list[float]],
        edges: list[tuple[int, int, float]],
        dim: int = 64,
        epochs: int = 5,
        lr: float = 0.025,
        neg_samples: int = 5,
        task_id: str = "",
    ) -> bool:
        """Отправить задачу обучения эмбеддингов в worker."""
        try:
            self._task_queue.put_nowait((
                "embeddings", task_id, vectors, edges, dim, epochs, lr, neg_samples,
            ))
            return True
        except Exception:
            return False

    def get_result(self, timeout: float = 0.01) -> Optional[tuple]:
        """
        Получить результат из worker (non-blocking по умолчанию).

        Returns:
            ("evolve_done", task_id, genomes, fitnesses, best_fitness)
            ("embeddings_done", task_id, vectors, avg_loss)
            None если результатов нет
        """
        try:
            return self._result_queue.get(timeout=timeout)
        except Exception:
            return None

    def drain_results(self) -> list[tuple]:
        """Забрать все готовые результаты (non-blocking)."""
        results = []
        while True:
            r = self.get_result(timeout=0.001)
            if r is None:
                break
            results.append(r)
        return results


def _worker_loop(
    task_queue: Queue,
    result_queue: Queue,
    running: "mp.Value[bool]",  # type: ignore[type-arg]
) -> None:
    """
    Главный цикл worker-процесса.

    Выполняется в ОТДЕЛЬНОМ процессе — полностью обходит GIL.
    """
    # Игнорируем SIGINT в worker (основной процесс обработает)
    signal.signal(signal.SIGINT, signal.SIG_IGN)

    # Попытка загрузить C-ускорение
    c_bridge = None
    try:
        from backend.service.c_evolve import CEvolveBridge
        c_bridge = CEvolveBridge()
        if c_bridge.available:
            _log_worker("C-ускорение загружено в worker-процессе")
        else:
            c_bridge = None
            _log_worker("C-ускорение недоступно, используем Python fallback")
    except Exception as e:
        _log_worker(f"Ошибка загрузки C: {e}")

    _log_worker(f"Worker запущен (PID={os.getpid()})")

    while running.value:
        try:
            task = task_queue.get(timeout=1.0)
        except Exception:
            continue

        kind = task[0]

        if kind == "shutdown":
            break

        try:
            if kind == "evolve":
                _, task_id, genomes, fitnesses, pairs, generations = task
                t0 = time.time()

                if c_bridge and c_bridge.available:
                    new_genomes, new_fitnesses, best = c_bridge.evolve(
                        genomes, fitnesses, pairs, generations,
                    )
                else:
                    new_genomes, new_fitnesses, best = _python_evolve(
                        genomes, fitnesses, pairs, generations,
                    )

                elapsed = time.time() - t0
                result_queue.put((
                    "evolve_done", task_id,
                    new_genomes, new_fitnesses, best, elapsed,
                ))

            elif kind == "embeddings":
                _, task_id, vectors, edges, dim, epochs, lr, neg_samples = task
                t0 = time.time()

                if c_bridge and c_bridge.available:
                    updated, loss = c_bridge.train_embeddings(
                        vectors, edges, dim, epochs, lr, neg_samples,
                    )
                else:
                    updated, loss = _python_train_embeddings(
                        vectors, edges, dim, epochs, lr, neg_samples,
                    )

                elapsed = time.time() - t0
                result_queue.put((
                    "embeddings_done", task_id,
                    updated, loss, elapsed,
                ))

        except Exception as e:
            _log_worker(f"Ошибка обработки {kind}: {e}")
            try:
                result_queue.put(("error", task[1] if len(task) > 1 else "", str(e)))
            except Exception:
                pass

    _log_worker("Worker завершён")


def _log_worker(msg: str) -> None:
    """Логирование из worker-процесса (print вместо logging)."""
    print(f"[TrainingWorker PID={os.getpid()}] {msg}", flush=True)


def _python_evolve(
    genomes: list[list[int]],
    fitnesses: list[float],
    semantic_pairs: list[tuple[list[int], list[int]]],
    generations: int,
) -> tuple[list[list[int]], list[float], float]:
    """Python fallback для evolve (если C недоступен)."""
    import math
    import random

    GENE_SIZE = 4000
    PATTERN_SIZE = 64
    MAX_EVAL = 60
    EVAL_DIGITS = 24
    n_pop = len(genomes)

    if not semantic_pairs:
        return genomes, fitnesses, 0.0

    # Выборка
    if len(semantic_pairs) <= MAX_EVAL:
        eval_sample = semantic_pairs
    else:
        recent = semantic_pairs[-MAX_EVAL // 2:]
        older = random.sample(
            semantic_pairs[:-MAX_EVAL // 2],
            min(MAX_EVAL // 2, len(semantic_pairs) - MAX_EVAL // 2),
        )
        eval_sample = recent + older

    best_fitness = 0.0
    prev_best = fitnesses[0] if fitnesses else 0.0

    for gen in range(generations):
        for f_idx in range(n_pop):
            genome = genomes[f_idx]
            total_sim = 0.0
            for src, tgt in eval_sample:
                pred = []
                for i in range(EVAL_DIGITS):
                    digit = src[i] if i < len(src) else 0
                    ctx = (src[(i + 1) % len(src)] + src[(i - 1) % len(src)]) * 0.05
                    x = (digit + i * 0.15 + ctx) / 12.0
                    # Simplified predict
                    for layer in range(0, min(len(genome), 80), 8):
                        op = genome[layer] % 12
                        slope = (1 if genome[layer + 1] % 2 == 0 else -1) * (genome[layer + 2] * 10 + genome[layer + 3]) * 0.01
                        bias = (1 if genome[layer + 4] % 2 == 0 else -1) * (genome[layer + 5] * 10 + genome[layer + 6]) * 0.01
                        if op == 0:
                            x = slope * x + bias
                        elif op == 6:
                            x = math.sin(x * 0.1) * slope + bias
                        elif op == 10:
                            x = math.tanh(x * 0.1) * slope + bias
                        else:
                            x = slope * x / (1.0 + abs(x)) + bias
                        if x > 5.0: x = 5.0
                        if x < -5.0: x = -5.0
                    pred.append(int(abs(x * 7.77)) % 10)

                sim = 0.0
                for j in range(min(EVAL_DIGITS, len(tgt))):
                    d = abs(pred[j] - tgt[j])
                    if d == 0: sim += 3.0
                    elif d == 1: sim += 2.0
                    elif d == 2: sim += 1.0
                    elif d <= 4: sim += 0.3
                sim /= (3.0 * EVAL_DIGITS)
                total_sim += sim

            fitnesses[f_idx] = total_sim / len(eval_sample) + 0.01 * _entropy(genome)

        # Sort
        paired = list(zip(fitnesses, genomes))
        paired.sort(key=lambda x: x[0], reverse=True)
        fitnesses = [p[0] for p in paired]
        genomes = [p[1] for p in paired]
        best_fitness = fitnesses[0]

        improvement = best_fitness - prev_best
        mutation_rate = 0.04 if improvement < 0.001 else 0.015
        prev_best = best_fitness

        elite = max(2, n_pop // 3)
        for i in range(elite, n_pop):
            p1 = random.randint(0, elite - 1)
            p2 = random.randint(0, elite - 1)
            # Crossover
            cut = random.randint(0, GENE_SIZE - 1)
            child = genomes[p1][:cut] + genomes[p2][cut:]
            # Mutate
            n_mut = max(1, int(GENE_SIZE * mutation_rate))
            for _ in range(n_mut):
                idx = random.randint(0, GENE_SIZE - 1)
                child[idx] = random.randint(0, 11)
            genomes[i] = child

    return genomes, fitnesses, best_fitness


def _entropy(genome: list[int]) -> float:
    """Нормализованная энтропия генома."""
    import math
    counts = [0] * 12
    for g in genome:
        counts[g % 12] += 1
    n = len(genome)
    ent = 0.0
    for c in counts:
        if c > 0:
            p = c / n
            ent -= p * math.log2(p)
    return ent / math.log2(12)


def _python_train_embeddings(
    vectors: dict[int, list[float]],
    edges: list[tuple[int, int, float]],
    dim: int,
    epochs: int,
    lr: float,
    neg_samples: int,
) -> tuple[dict[int, list[float]], float]:
    """Python fallback для embedding training."""
    import math
    import random

    hash_list = sorted(vectors.keys())
    if len(hash_list) < 2 or not edges:
        return vectors, 0.0

    total_loss = 0.0
    total_pairs = 0

    for epoch in range(epochs):
        current_lr = lr * (1.0 - epoch / (epochs + 1))
        random.shuffle(edges)

        for src_h, tgt_h, weight in edges:
            if src_h not in vectors or tgt_h not in vectors:
                continue
            v1 = vectors[src_h]
            v2 = vectors[tgt_h]
            reps = max(1, min(4, int(weight * 3)))

            for _ in range(reps):
                dot = sum(a * b for a, b in zip(v1, v2))
                sig = 1.0 / (1.0 + math.exp(-max(-15, min(15, dot))))
                grad = (1.0 - sig) * current_lr
                for d in range(dim):
                    g1 = grad * v2[d]
                    g2 = grad * v1[d]
                    v1[d] += g1
                    v2[d] += g2
                total_loss += -math.log(sig + 1e-10)
                total_pairs += 1

                for _ in range(neg_samples):
                    neg_h = random.choice(hash_list)
                    if neg_h == src_h or neg_h == tgt_h:
                        continue
                    v_neg = vectors[neg_h]
                    dot = sum(a * b for a, b in zip(v1, v_neg))
                    sig = 1.0 / (1.0 + math.exp(-max(-15, min(15, dot))))
                    neg_grad = -sig * current_lr * 0.5
                    for d in range(dim):
                        v1[d] += neg_grad * v_neg[d]
                        v_neg[d] += neg_grad * v1[d]

    return vectors, total_loss / max(total_pairs, 1)
