#!/usr/bin/env python3
"""
swarm_100.py — Оркестратор 100 параллельных узлов Kolibri

Запускает N узлов kolibri_node, каждый учится на своём подмножестве данных
и синхронизирует знания через coordinator.

Архитектура:
  ┌──────────────┐     ┌──────────────┐
  │ Orchestrator │────→│ Coordinator  │← формулы, знания
  │ (этот скрипт)│     │ :9900        │
  └──────┬───────┘     └──────────────┘
         │                    ↑ ↓
    ┌────┴────────────────────┴──┐
    │   N × kolibri_node         │
    │   :9901, :9902, ..., :99XX │
    │   Каждый → teach + evolve  │
    └────────────────────────────┘

Использование:
    python scripts/swarm_100.py --nodes 100 --corpus data/corpus
    python scripts/swarm_100.py --nodes 50 --ticks 1000
"""
from __future__ import annotations

import argparse
import json
import logging
import os
import signal
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Optional

logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
)
log = logging.getLogger("swarm")

_PROJECT_ROOT = Path("/workspaces/kolibri-project")
_NODE_BIN = _PROJECT_ROOT / "build" / "kolibri_node"
_COORDINATOR_BIN = _PROJECT_ROOT / "build" / "kolibri_coordinator"
_GENOME_DIR = _PROJECT_ROOT / "data" / "genomes"
_CORPUS_DIR = _PROJECT_ROOT / "data" / "corpus"
_LOG_DIR = _PROJECT_ROOT / "logs" / "swarm"

# Базовый порт (coordinator на BASE, nodes на BASE+1..BASE+N)
BASE_PORT = 9900


@dataclass
class NodeInfo:
    """Информация об одном узле."""
    node_id: int
    port: int
    process: Optional[subprocess.Popen] = None
    genome_path: str = ""
    log_path: str = ""
    status: str = "stopped"  # stopped | running | error | finished
    patterns_learned: int = 0
    fitness: float = 0.0
    start_time: float = 0.0


@dataclass
class SwarmState:
    """Общее состояние роя."""
    nodes: list[NodeInfo] = field(default_factory=list)
    coordinator: Optional[subprocess.Popen] = None
    total_patterns: int = 0
    avg_fitness: float = 0.0
    sync_count: int = 0
    start_time: float = field(default_factory=time.time)


class SwarmOrchestrator:
    """
    Оркестратор для запуска и управления роем Kolibri узлов.

    Каждый узел:
    1. Получает своё подмножество корпуса
    2. Обучается (teach + evolve)
    3. Синхронизирует лучшие формулы через coordinator
    4. Импортирует знания от соседей
    """

    def __init__(
        self,
        n_nodes: int = 100,
        ticks_per_node: int = 500,
        corpus_dir: Path = _CORPUS_DIR,
    ) -> None:
        self.n_nodes = n_nodes
        self.ticks_per_node = ticks_per_node
        self.corpus_dir = corpus_dir
        self.state = SwarmState()
        self._shutdown = False

        # Создаём директории
        _GENOME_DIR.mkdir(parents=True, exist_ok=True)
        _LOG_DIR.mkdir(parents=True, exist_ok=True)

    def start(self) -> None:
        """Запустить весь рой."""
        log.info(
            "🐝 Запуск роя: %d узлов, %d тиков каждый",
            self.n_nodes, self.ticks_per_node,
        )

        # Проверка бинарников
        if not _NODE_BIN.exists():
            log.error("❌ Бинарник узла не найден: %s", _NODE_BIN)
            log.info("Компиляция: cmake -S . -B build -G Ninja && cmake --build build")
            return

        # 1. Запуск coordinator
        self._start_coordinator()

        # 2. Разделение корпуса
        corpus_files = self._split_corpus()

        # 3. Запуск узлов порциями (по 10)
        batch_size = 10
        for batch_start in range(0, self.n_nodes, batch_size):
            batch_end = min(batch_start + batch_size, self.n_nodes)
            for i in range(batch_start, batch_end):
                node_corpus = corpus_files[i] if i < len(corpus_files) else None
                self._start_node(i, node_corpus)
            log.info("  Запущена порция узлов %d–%d", batch_start, batch_end - 1)
            time.sleep(0.5)

        log.info("✅ Запущено %d узлов", len(self.state.nodes))

        # 4. Мониторинг
        signal.signal(signal.SIGINT, self._handle_signal)
        signal.signal(signal.SIGTERM, self._handle_signal)
        self._monitor_loop()

    def _start_coordinator(self) -> None:
        """Запуск coordinator для синхронизации формул."""
        if not _COORDINATOR_BIN.exists():
            log.warning(
                "⚠️ Coordinator не найден: %s (работаем без синхронизации)",
                _COORDINATOR_BIN,
            )
            return

        port = BASE_PORT
        targets = ",".join(
            f"127.0.0.1:{BASE_PORT + 1 + i}" for i in range(self.n_nodes)
        )

        log_path = _LOG_DIR / "coordinator.log"
        log_file = open(log_path, "w")

        cmd = [
            str(_COORDINATOR_BIN),
            "--listen", str(port),
            "--targets", targets,
        ]
        proc = subprocess.Popen(
            cmd, stdout=log_file, stderr=subprocess.STDOUT,
            cwd=str(_PROJECT_ROOT),
        )
        self.state.coordinator = proc
        log.info("📡 Coordinator запущен на :%d (PID=%d)", port, proc.pid)

    def _start_node(self, node_id: int, corpus_file: Optional[Path]) -> None:
        """Запуск одного узла."""
        port = BASE_PORT + 1 + node_id
        genome_path = _GENOME_DIR / f"node_{node_id:04d}.dat"
        log_path = _LOG_DIR / f"node_{node_id:04d}.log"

        info = NodeInfo(
            node_id=node_id,
            port=port,
            genome_path=str(genome_path),
            log_path=str(log_path),
        )

        # Формируем команды для stdin
        ks_commands = []

        # Обучение на корпусе
        if corpus_file and corpus_file.exists():
            ks_commands.append(f":mass-learn {corpus_file}")

        # Подключение к coordinator
        coordinator_port = BASE_PORT
        ks_commands.append(f":peer 127.0.0.1:{coordinator_port}")

        # Эволюция с периодической синхронизацией
        sync_interval = max(50, self.ticks_per_node // 10)
        remaining = self.ticks_per_node
        while remaining > 0:
            batch = min(sync_interval, remaining)
            ks_commands.append(f":tick {batch}")
            ks_commands.append(":sync")
            remaining -= batch

        # Сохранение и выход
        ks_commands.append(f":save {genome_path}")
        ks_commands.append(":quit")

        input_text = "\n".join(ks_commands) + "\n"

        log_file = open(log_path, "w")
        cmd = [str(_NODE_BIN)]
        if genome_path.exists():
            cmd.extend(["--genome", str(genome_path)])
        cmd.extend(["--listen", str(port)])

        try:
            proc = subprocess.Popen(
                cmd,
                stdin=subprocess.PIPE,
                stdout=log_file,
                stderr=subprocess.STDOUT,
                cwd=str(_PROJECT_ROOT),
            )
            # Отправляем команды
            if proc.stdin:
                proc.stdin.write(input_text.encode())
                proc.stdin.flush()
                proc.stdin.close()

            info.process = proc
            info.status = "running"
            info.start_time = time.time()
            self.state.nodes.append(info)

        except Exception as e:
            log.error("❌ Ошибка запуска узла %d: %s", node_id, e)
            info.status = "error"
            self.state.nodes.append(info)

    def _split_corpus(self) -> list[Optional[Path]]:
        """Разделить корпус между узлами."""
        if not self.corpus_dir.exists():
            log.warning("Корпус не найден: %s", self.corpus_dir)
            return [None] * self.n_nodes

        files = sorted(self.corpus_dir.rglob("*.txt"))
        if not files:
            return [None] * self.n_nodes

        # Равномерное распределение файлов по узлам
        chunks: list[list[Path]] = [[] for _ in range(self.n_nodes)]
        for i, f in enumerate(files):
            chunks[i % self.n_nodes].append(f)

        # Создаём объединённые файлы для каждого узла
        result: list[Optional[Path]] = []
        list_dir = _LOG_DIR / "corpus_lists"
        list_dir.mkdir(parents=True, exist_ok=True)

        for i, chunk in enumerate(chunks):
            if not chunk:
                result.append(None)
                continue
            merged_path = list_dir / f"node_{i:04d}_corpus.txt"
            with open(merged_path, "w", encoding="utf-8") as f:
                for txt_file in chunk:
                    try:
                        content = txt_file.read_text(encoding="utf-8", errors="ignore")
                        f.write(content)
                        f.write("\n\n")
                    except OSError:
                        continue
            result.append(merged_path)

        log.info(
            "📚 Корпус: %d файлов → %d узлов (по ~%d файлов)",
            len(files), self.n_nodes,
            len(files) // max(self.n_nodes, 1),
        )
        return result

    def _monitor_loop(self) -> None:
        """Цикл мониторинга здоровья узлов."""
        while not self._shutdown:
            time.sleep(10)

            running = 0
            finished = 0
            errors = 0

            for node in self.state.nodes:
                if node.process is None:
                    continue
                ret = node.process.poll()
                if ret is None:
                    running += 1
                elif ret == 0:
                    node.status = "finished"
                    finished += 1
                else:
                    node.status = "error"
                    errors += 1

            elapsed = time.time() - self.state.start_time
            log.info(
                "🐝 Рой [%.0fs]: %d работают, %d завершены, %d ошибок",
                elapsed, running, finished, errors,
            )

            # Все завершились?
            if running == 0 and len(self.state.nodes) > 0:
                log.info("🏁 Все узлы завершили работу!")
                break

        self._cleanup()

    def _cleanup(self) -> None:
        """Остановить все процессы."""
        log.info("🛑 Остановка роя...")

        for node in self.state.nodes:
            if node.process and node.process.poll() is None:
                node.process.terminate()
                try:
                    node.process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    node.process.kill()

        if self.state.coordinator and self.state.coordinator.poll() is None:
            self.state.coordinator.terminate()
            try:
                self.state.coordinator.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.state.coordinator.kill()

        self._print_summary()

    def _print_summary(self) -> None:
        """Финальная сводка."""
        elapsed = time.time() - self.state.start_time
        finished = sum(1 for n in self.state.nodes if n.status == "finished")
        errors = sum(1 for n in self.state.nodes if n.status == "error")
        total_genomes = sum(
            1 for n in self.state.nodes if Path(n.genome_path).exists()
        )

        print(f"""
{'='*60}
🐝 РЕЗУЛЬТАТЫ РОЕВОГО ОБУЧЕНИЯ
{'='*60}
  Узлов запущено:    {len(self.state.nodes)}
  Успешно завершены: {finished}
  С ошибками:        {errors}
  Геномов сохранено: {total_genomes}
  Время работы:      {elapsed:.0f} сек ({elapsed/60:.1f} мин)
{'='*60}
  Геномы: {_GENOME_DIR}
  Логи:   {_LOG_DIR}
""")

    def _handle_signal(self, signum: int, frame) -> None:
        log.info("⚠️ Сигнал %d, остановка...", signum)
        self._shutdown = True


def main() -> None:
    parser = argparse.ArgumentParser(description="Kolibri Swarm 100 Nodes")
    parser.add_argument("--nodes", type=int, default=100, help="Количество узлов")
    parser.add_argument("--ticks", type=int, default=500, help="Тиков на узел")
    parser.add_argument("--corpus", type=str, default=str(_CORPUS_DIR), help="Корпус")
    args = parser.parse_args()

    orchestrator = SwarmOrchestrator(
        n_nodes=args.nodes,
        ticks_per_node=args.ticks,
        corpus_dir=Path(args.corpus),
    )
    orchestrator.start()


if __name__ == "__main__":
    main()
