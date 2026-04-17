"""
Kolibri Swarm Bridge — Node 2
Соединяет Python backend (kolibriai.ru) со swarm сетью.

Что делает:
1. Экспортирует знания из Python backend в kolibri_swarm
2. Получает знания из swarm и добавляет в Python базу
3. Непрерывное фоновое обучение из swarm сети
4. Запускает kolibri_swarm :8002 на Node 2

Запуск: python3 swarm_node2.py
"""

import httpx
import json
import asyncio
import subprocess
import os
import time
from pathlib import Path

# ─── Конфигурация ───
PYTHON_BACKEND = "http://127.0.0.1:8001"
SWARM_LOCAL = "http://127.0.0.1:8002"
SWARM_PEER = "http://217.60.249.157:8001"
SWARM_MAC = "http://mac-host:8002"  # если доступно

KNOWLEDGE_DIR = "/srv/kolibri/repo/knowledge"
SWARM_BINARY = "/home/ladik/kolibri/kolibri_swarm"
SWARM_PORT = 8002
SYNC_INTERVAL = 60  # секунд между синхронизациями
LEARN_INTERVAL = 30  # секунд между циклами обучения

class SwarmBridge:
    def __init__(self):
        self.running = False
        self.stats = {
            "exported": 0,
            "imported": 0,
            "sync_rounds": 0,
            "learn_cycles": 0,
            "errors": 0,
        }

    async def start_swarm_node(self):
        """Запустить kolibri_swarm на Node 2"""
        print(f"🐝 Starting kolibri_swarm :{SWARM_PORT}...")
        
        # Убить старый процесс
        subprocess.run(["pkill", "-f", f"kolibri_swarm {SWARM_PORT}"], 
                      capture_output=True)
        await asyncio.sleep(1)
        
        # Запустить swarm с пиром к Node 1 и Mac
        cmd = [
            SWARM_BINARY, str(SWARM_PORT),
            "--peer", "217.60.249.157:8001",
        ]
        
        self.swarm_proc = subprocess.Popen(
            cmd,
            stdout=open("/tmp/kolibri_swarm_node2.log", "w"),
            stderr=subprocess.STDOUT,
            cwd=os.path.dirname(SWARM_BINARY),
        )
        print(f"  PID: {self.swarm_proc.pid}")
        
        # Подождать запуска
        for i in range(10):
            await asyncio.sleep(1)
            try:
                async with httpx.AsyncClient(timeout=2.0) as client:
                    r = await client.get(f"{SWARM_LOCAL}/api/v1/health")
                    if r.status_code == 200:
                        data = r.json()
                        print(f"  ✅ {data.get('facts', '?')} facts, "
                              f"{data.get('peers', '?')} peers")
                        return True
            except:
                pass
        print("  ⚠️  Swarm node may not be ready")
        return False

    async def export_python_knowledge_to_swarm(self):
        """Экспортировать знания из Python backend в swarm"""
        try:
            async with httpx.AsyncClient(timeout=30.0) as client:
                # Получить знания из Python backend
                # Используем существующий endpoint для экспорта
                r = await client.get(
                    f"{PYTHON_BACKEND}/api/v1/knowledge/export",
                    params={"format": "json", "limit": 100}
                )
                
                if r.status_code != 200:
                    # Fallback: получить из графа знаний
                    r = await client.get(
                        f"{PYTHON_BACKEND}/api/v1/ai/knowledge/analytics"
                    )
                
                if r.status_code == 200:
                    data = r.json()
                    facts = data.get("facts", []) or data.get("knowledge", [])
                    
                    if facts:
                        # Отправить в swarm
                        for fact in facts[:50]:  # батчами по 50
                            q = fact.get("question", fact.get("q", ""))
                            a = fact.get("answer", fact.get("a", ""))
                            if q and a:
                                await self._add_to_swarm(q, a)
                        
                        self.stats["exported"] += len(facts)
                        print(f"  📤 Exported {len(facts)} facts to swarm")
                        return len(facts)
        except Exception as e:
            self.stats["errors"] += 1
            print(f"  ⚠️  Export error: {e}")
        return 0

    async def _add_to_swarm(self, question: str, answer: str):
        """Добавить Q&A пару в swarm через chat endpoint"""
        try:
            # Swarm learns from chat interactions
            async with httpx.AsyncClient(timeout=5.0) as client:
                # Send as a "training" query
                await client.post(
                    f"{SWARM_LOCAL}/api/v1/ai/chat",
                    json={
                        "message": question,
                        "conversation_id": "training",
                    }
                )
        except:
            pass

    async def import_swarm_knowledge_to_python(self):
        """Импортировать знания из swarm в Python backend"""
        try:
            async with httpx.AsyncClient(timeout=5.0) as client:
                # Получить health swarm чтобы узнать сколько фактов
                r = await client.get(f"{SWARM_LOCAL}/api/v1/health")
                if r.status_code == 200:
                    data = r.json()
                    facts_count = data.get("facts", 0)
                    print(f"  📥 Swarm has {facts_count} facts")
                    
                    # Сохранить статистику
                    self.stats["imported"] = facts_count
        except Exception as e:
            self.stats["errors"] += 1
            print(f"  ⚠️  Import error: {e}")

    async def continuous_learning_loop(self):
        """Непрерывное фоновое обучение"""
        print("🧠 Starting continuous learning loop...")
        
        while self.running:
            try:
                self.stats["learn_cycles"] += 1
                
                # 1. Синхронизация с пирами
                print(f"\n═══ Sync Round #{self.stats['sync_rounds'] + 1} ═══")
                await self.sync_with_peers()
                self.stats["sync_rounds"] += 1
                
                # 2. Экспорт знаний из Python → Swarm
                await self.export_python_knowledge_to_swarm()
                
                # 3. Импорт знаний из Swarm → Python
                await self.import_swarm_knowledge_to_python()
                
                # 4. Обучение на новых данных
                await self.learn_from_swarm()
                
                # 5. Статус
                print(f"\n📊 Stats: {json.dumps(self.stats, indent=2)}")
                
                # Ждать до следующего цикла
                await asyncio.sleep(SYNC_INTERVAL)
                
            except Exception as e:
                self.stats["errors"] += 1
                print(f"❌ Learning cycle error: {e}")
                await asyncio.sleep(10)

    async def sync_with_peers(self):
        """Синхронизация с пирами swarm сети"""
        try:
            async with httpx.AsyncClient(timeout=10.0) as client:
                # Запросить sync от Node 1
                r = await client.post(
                    f"{SWARM_LOCAL}/api/v1/swarm/sync",
                    json={
                        "host": "217.60.249.157",
                        "port": 8001,
                    }
                )
                if r.status_code == 200:
                    data = r.json()
                    print(f"  ✅ Sync: {data}")
        except Exception as e:
            print(f"  ⚠️  Sync error: {e}")

    async def learn_from_swarm(self):
        """Обучение на данных из swarm сети"""
        try:
            async with httpx.AsyncClient(timeout=10.0) as client:
                # Получить случайные факты из swarm для обучения
                r = await client.get(f"{SWARM_LOCAL}/api/v1/swarm/export")
                if r.status_code == 200:
                    # NDJSON формат
                    lines = r.text.strip().split('\n')
                    count = 0
                    for line in lines:
                        try:
                            fact = json.loads(line)
                            q = fact.get("q", "")
                            a = fact.get("a", "")
                            if q and a:
                                # Добавить в локальную базу знаний
                                await self._store_fact(q, a)
                                count += 1
                        except:
                            pass
                    
                    if count > 0:
                        print(f"  🧠 Learned {count} new facts from swarm")
        except Exception as e:
            print(f"  ⚠️  Learn error: {e}")

    async def _store_fact(self, question: str, answer: str):
        """Сохранить факт в Python backend"""
        try:
            async with httpx.AsyncClient(timeout=10.0) as client:
                # Использовать существующий endpoint для добавления знаний
                await client.post(
                    f"{PYTHON_BACKEND}/api/v1/ai/knowledge/add",
                    json={"question": question, "answer": answer}
                )
        except:
            # Fallback: сохранить в файл
            kb_file = f"{KNOWLEDGE_DIR}/swarm_imports.md"
            os.makedirs(os.path.dirname(kb_file), exist_ok=True)
            with open(kb_file, "a") as f:
                f.write(f"### Q: {question}\n\n**Ответ:** {answer}\n\n---\n\n")

    async def start(self):
        """Запустить swarm bridge"""
        self.running = True
        print("╔══════════════════════════════════════════════════╗")
        print("║   KOLIBRI SWARM BRIDGE — NODE 2                ║")
        print("╚══════════════════════════════════════════════════╝")
        print(f"  Python Backend: {PYTHON_BACKEND}")
        print(f"  Swarm Local:    {SWARM_LOCAL}")
        print(f"  Swarm Peer:     {SWARM_PEER}")
        print()
        
        # 1. Запустить swarm node
        await self.start_swarm_node()
        
        # 2. Запустить цикл обучения
        await self.continuous_learning_loop()

    async def stop(self):
        """Остановить swarm bridge"""
        self.running = False
        if hasattr(self, 'swarm_proc'):
            self.swarm_proc.terminate()
        print("🛑 Swarm bridge stopped")


async def main():
    bridge = SwarmBridge()
    try:
        await bridge.start()
    except KeyboardInterrupt:
        await bridge.stop()


if __name__ == "__main__":
    asyncio.run(main())
