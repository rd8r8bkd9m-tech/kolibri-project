#!/usr/bin/env python3
"""
Диагностика и оптимизация памяти для Kolibri swarm кластера.
Дает рекомендации по количеству узлов и параметрам.

Использование:
    python3 optimize_swarm_config.py              # Анализировать текущую систему
    python3 optimize_swarm_config.py --nodes 50   # Рекомендации для 50 узлов
"""
from __future__ import annotations

import os
import sys
import subprocess
import psutil
import json
from pathlib import Path
from typing import Optional

class SwarmOptimizer:
    """Анализатор и оптимизатор конфигурации swarm кластера."""
    
    # Примерное потребление памяти на узел (MB)
    MEMORY_PER_NODE = {
        "base": 25,           # FastAPI + uvicorn базовый
        "learning": 35,       # +continuous learning daemon
        "background": 20,     # +background learning manager
        "swarm_sync": 10,     # +свопы синхронизации
    }
    
    def __init__(self):
        self.project_root = Path(__file__).parent.parent
        self.total_memory = psutil.virtual_memory().total / (1024 ** 2)  # MB
        self.available_memory = psutil.virtual_memory().available / (1024 ** 2)  # MB
        
    def get_system_info(self) -> dict:
        """Информация о системе."""
        vm = psutil.virtual_memory()
        return {
            "total_memory_mb": int(vm.total / (1024 ** 2)),
            "available_memory_mb": int(vm.available / (1024 ** 2)),
            "used_memory_mb": int(vm.used / (1024 ** 2)),
            "memory_percent": vm.percent,
            "cpu_count": psutil.cpu_count(),
            "swap_total_mb": int(psutil.swap_memory().total / (1024 ** 2)),
        }
    
    def get_active_nodes_info(self) -> dict:
        """Информация об активных узлах."""
        try:
            result = subprocess.run(
                "pgrep -f 'uvicorn.*kolibri' | wc -l",
                shell=True, capture_output=True, text=True
            )
            count = int(result.stdout.strip())
            
            # Память всех узлов
            result = subprocess.run(
                "ps aux | grep 'uvicorn' | grep -v grep | awk '{s+=$6} END {print s}'",
                shell=True, capture_output=True, text=True
            )
            total_mem_kb = int(result.stdout.strip()) if result.stdout.strip() else 0
            
            avg_mem = (total_mem_kb / 1024 / count) if count > 0 else 0
            
            return {
                "active_nodes": count,
                "total_memory_mb": int(total_mem_kb / 1024),
                "avg_memory_per_node_mb": int(avg_mem),
            }
        except Exception as e:
            return {"error": str(e)}
    
    def calculate_optimal_config(self, target_nodes: int) -> dict:
        """Рассчитать оптимальную конфигурацию для N узлов."""
        
        # Разделяем узлы на две группы
        learning_nodes = max(1, target_nodes // 10)  # 10% узлов обучаются
        sync_nodes = target_nodes - learning_nodes
        
        # Потребление памяти для каждой группы
        learning_mem_per_node = sum(self.MEMORY_PER_NODE.values())  # base + learning + background + swarm
        sync_mem_per_node = self.MEMORY_PER_NODE["base"] + self.MEMORY_PER_NODE["swarm_sync"]
        
        total_estimated = (
            learning_nodes * learning_mem_per_node +
            sync_nodes * sync_mem_per_node
        )
        
        safe_threshold = self.total_memory * 0.75  # Используем 75% памяти
        
        return {
            "target_nodes": target_nodes,
            "recommended_learning_nodes": learning_nodes,
            "recommended_sync_nodes": sync_nodes,
            "estimated_memory_mb": int(total_estimated),
            "available_memory_mb": int(self.available_memory),
            "safe_memory_threshold_mb": int(safe_threshold),
            "is_feasible": total_estimated < safe_threshold,
            "recommended_batch_size": max(3, min(10, self.total_memory / 4 / learning_mem_per_node)),
            "recommended_batch_delay_sec": 2 if total_estimated < safe_threshold * 0.8 else 3,
            "recommended_memory_limit_pct": 75 if total_estimated < safe_threshold * 0.9 else 60,
        }
    
    def get_recommendations(self, config: dict) -> list[str]:
        """Рекомендации по оптимизации."""
        reqs = []
        
        if not config.get("is_feasible"):
            reqs.append("❌ НЕВОЗМОЖНО: Требуется слишком много памяти")
            reqs.append(f"   Нужно: {config['estimated_memory_mb']} MB")
            reqs.append(f"   Доступно: {config['available_memory_mb']} MB")
            reqs.append(f"   Максимум узлов: ~{int(config['available_memory_mb'] / 60)}")
        else:
            reqs.append("✅ ВОЗМОЖНО: Конфигурация имеет запас памяти")
        
        reqs.append("")
        
        if config['estimated_memory_mb'] > config['available_memory_mb'] * 0.8:
            reqs.append("⚠️  Используется > 80% памяти")
            reqs.append("   • Увеличьте batch_delay для спокойного старта")
            reqs.append("   • Запустите меньше learning узлов")
            reqs.append("   • Закройте другие приложения")
        
        reqs.append("")
        reqs.append("🔧 РЕКОМЕНДУЕМАЯ КОНФИГУРАЦИЯ:")
        reqs.append(f"  • Обучающих узлов: {config['recommended_learning_nodes']}")
        reqs.append(f"  • Sync-only узлов: {config['recommended_sync_nodes']}")
        reqs.append(f"  • Размер партии: {config['recommended_batch_size']}")
        reqs.append(f"  • Пауза между партиями: {config['recommended_batch_delay_sec']} сек")
        reqs.append(f"  • Лимит памяти для отсечки: {config['recommended_memory_limit_pct']}%")
        
        reqs.append("")
        reqs.append("💡 ОПТИМИЗАЦИОННЫЕ СОВЕТЫ:")
        reqs.append("  • Отключите KOLIBRI_ENABLE_CONTINUOUS_LEARNING для большинства узлов")
        reqs.append("  • Используйте --workers 1 (отключить multiprocessing)")
        reqs.append("  • Установите нулевые временные задержки: --timeout 0")
        reqs.append("  • Используйте localhost (127.0.0.1) вместо 0.0.0.0")
        reqs.append("  • Запускайте партиями, не все сразу")
        
        return reqs
    
    def analyze_and_report(self):
        """Полный анализ и отчет."""
        print("=" * 70)
        print("АНАЛИЗ KOLIBRI SWARM ПАМЯТИ И КОНФИГУРАЦИИ")
        print("=" * 70)
        print()
        
        # Система
        print("📊 СИСТЕМА:")
        sys_info = self.get_system_info()
        for key, value in sys_info.items():
            key_display = key.replace("_", " ").title()
            print(f"  {key_display}: {value}")
        print()
        
        # Активные узлы
        print("🖥️  АКТИВНЫЕ УЗЛЫ:")
        nodes_info = self.get_active_nodes_info()
        if "error" not in nodes_info:
            for key, value in nodes_info.items():
                key_display = key.replace("_", " ").title()
                print(f"  {key_display}: {value}")
        else:
            print(f"  Ошибка: {nodes_info['error']}")
        print()
        
        # Рекомендации для разных масштабов
        print("📈 РЕКОМЕНДАЦИИ ДЛЯ РАЗНЫХ МАСШТАБОВ:")
        for target in [15, 25, 50]:
            config = self.calculate_optimal_config(target)
            status = "✅" if config["is_feasible"] else "❌"
            mem_used = config["estimated_memory_mb"]
            print(f"  {status} {target:2d} узлов: ~{mem_used:5d} MB")
            if not config["is_feasible"]:
                print(f"       ^ Слишком много (доступно: {config['available_memory_mb']} MB)")
        print()


def main():
    optimizer = SwarmOptimizer()
    
    if len(sys.argv) > 1:
        if sys.argv[1] == "--nodes" and len(sys.argv) > 2:
            target = int(sys.argv[2])
            config = optimizer.calculate_optimal_config(target)
            print(json.dumps(config, indent=2))
            print()
            for rec in optimizer.get_recommendations(config):
                print(rec)
        else:
            optimizer.analyze_and_report()
    else:
        optimizer.analyze_and_report()


if __name__ == "__main__":
    main()
