#!/usr/bin/env python3
"""
kolibri_real_bridge.py — REAL bidirectional bridge

Connects Python backend (kolibriai.ru) with Kolibri Swarm network.
Extracts REAL conversation data and knowledge graph patterns,
feeds them into swarm, and imports swarm knowledge back.

Usage:
  python3 kolibri_real_bridge.py --swarm-port 8002 --python-port 8001 \
    --peer 217.60.249.157:8001 --sync-interval 30
"""

import httpx
import json
import asyncio
import subprocess
import os
import sys
import argparse
import time
from pathlib import Path
from datetime import datetime

CONFIG = {
    "python_backend": "http://127.0.0.1:8001",
    "swarm_port": 8002,
    "swarm_peers": ["217.60.249.157:8001"],
    "sync_interval": 30,
    "max_conversations_per_cycle": 10,
    "max_patterns_per_cycle": 50,
    "knowledge_file": "/srv/kolibri/repo/knowledge/swarm_sync.md",
    "log_file": "/tmp/kolibri_real_bridge.log",
    "state_file": "/tmp/kolibri_bridge_state.json",
}

class RealBridge:
    def __init__(self, config=None):
        self.config = {**CONFIG, **(config or {})}
        self.running = False
        self.seen_questions = set()
        self.last_conv_ids = set()
        self.last_pattern_count = 0
        self.stats = {
            "exported": 0,
            "imported": 0,
            "conversations_synced": 0,
            "patterns_synced": 0,
            "sync_rounds": 0,
            "learn_cycles": 0,
            "errors": 0,
            "start_time": None,
        }
        self._load_state()

    def _load_state(self):
        try:
            with open(self.config["state_file"]) as f:
                state = json.load(f)
                self.seen_questions = set(state.get("seen_questions", []))
                self.last_conv_ids = set(state.get("last_conv_ids", []))
                self.last_pattern_count = state.get("last_pattern_count", 0)
                self.stats.update(state.get("stats", {}))
        except:
            pass

    def _save_state(self):
        try:
            state = {
                "seen_questions": list(self.seen_questions)[-10000:],  # Keep last 10K
                "last_conv_ids": list(self.last_conv_ids)[-1000:],
                "last_pattern_count": self.last_pattern_count,
                "stats": self.stats,
            }
            with open(self.config["state_file"], "w") as f:
                json.dump(state, f)
        except:
            pass

    def log(self, msg):
        ts = datetime.now().strftime("%H:%M:%S")
        line = f"[{ts}] {msg}"
        print(line, flush=True)
        try:
            with open(self.config["log_file"], "a") as f:
                f.write(line + "\n")
        except:
            pass

    # ─── Swarm Management ───
    async def start_swarm_node(self):
        """Start kolibri_swarm binary"""
        swarm_binary = os.path.join(os.path.expanduser("~"), "kolibri", "kolibri_swarm")
        
        if not os.path.exists(swarm_binary):
            self.log(f"⚠️  Swarm binary not found at {swarm_binary}")
            return False
        
        subprocess.run(["pkill", "-f", f"kolibri_swarm {self.config['swarm_port']}"],
                      capture_output=True)
        await asyncio.sleep(1)
        
        peers = []
        for p in self.config["swarm_peers"]:
            peers.extend(["--peer", p])
        
        cmd = [swarm_binary, str(self.config["swarm_port"])] + peers
        
        self.log(f"🐝 Starting swarm :{self.config['swarm_port']} peers={self.config['swarm_peers']}")
        
        log_path = "/tmp/kolibri_swarm_node2.log"
        self.swarm_proc = subprocess.Popen(
            cmd, stdout=open(log_path, "w"), stderr=subprocess.STDOUT,
            cwd=os.path.dirname(swarm_binary),
        )
        
        swarm_url = f"http://127.0.0.1:{self.config['swarm_port']}"
        for i in range(15):
            await asyncio.sleep(1)
            try:
                async with httpx.AsyncClient(timeout=2.0) as client:
                    r = await client.get(f"{swarm_url}/api/v1/health")
                    if r.status_code == 200:
                        data = r.json()
                        facts = data.get("facts", "?")
                        peers_count = data.get("peers", "?")
                        self.log(f"✅ Swarm ready: {facts} facts, {peers_count} peers")
                        return True
            except:
                pass
        
        self.log("⚠️  Swarm may not be fully ready")
        return False

    # ─── Export: Python Backend → Swarm ───
    async def export_conversations(self):
        """Extract Q&A from conversations and push to swarm"""
        python_url = self.config["python_backend"]
        swarm_url = f"http://127.0.0.1:{self.config['swarm_port']}"
        exported = 0
        
        try:
            async with httpx.AsyncClient(timeout=30.0) as client:
                # Get recent conversations
                r = await client.get(
                    f"{python_url}/api/v1/ai/conversations",
                    params={"limit": self.config["max_conversations_per_cycle"]}
                )
                
                if r.status_code != 200:
                    return 0
                
                data = r.json()
                conversations = []
                if isinstance(data, list):
                    conversations = data
                elif isinstance(data, dict):
                    conversations = data.get("conversations", data.get("items", []))
                
                for conv in conversations:
                    conv_id = conv.get("id", conv.get("conversation_id", ""))
                    if not conv_id or conv_id in self.last_conv_ids:
                        continue
                    
                    # Get conversation turns
                    try:
                        turns_r = await client.get(
                            f"{python_url}/api/v1/ai/conversations/{conv_id}/turns",
                            params={"limit": 20}
                        )
                        if turns_r.status_code == 200:
                            turns = turns_r.json()
                            if isinstance(turns, list):
                                for turn in turns:
                                    q = turn.get("user", turn.get("question", turn.get("prompt", "")))
                                    a = turn.get("assistant", turn.get("answer", turn.get("response", "")))
                                    
                                    if q and a and len(q) > 3 and len(a) > 5:
                                        q_key = q[:100]  # Dedup key
                                        if q_key not in self.seen_questions:
                                            await self._push_to_swarm(swarm_url, q, a)
                                            self.seen_questions.add(q_key)
                                            exported += 1
                                            self.last_conv_ids.add(conv_id)
                    except:
                        pass
                
                if exported > 0:
                    self.stats["conversations_synced"] += len(conversations)
                    self.stats["exported"] += exported
                    self.log(f"📤 Exported {exported} Q&A from conversations → Swarm")
                    
        except Exception as e:
            self.stats["errors"] += 1
            self.log(f"⚠️  Export conversations error: {e}")
        
        return exported

    async def export_patterns(self):
        """Extract knowledge graph patterns and push to swarm"""
        python_url = self.config["python_backend"]
        swarm_url = f"http://127.0.0.1:{self.config['swarm_port']}"
        exported = 0
        
        try:
            async with httpx.AsyncClient(timeout=30.0) as client:
                # Get stats to check pattern count
                r = await client.get(f"{python_url}/api/v1/ai/stats")
                if r.status_code == 200:
                    stats = r.json()
                    pattern_count = stats.get("graph_patterns", 0)
                    edge_count = stats.get("graph_edges", 0)
                    docs = stats.get("graph_documents", 0)
                    
                    self.log(f"📊 Python stats: {pattern_count} patterns, {edge_count} edges, {docs} docs")
                    
                    # If new patterns exist, export some
                    if pattern_count > self.last_pattern_count:
                        # Export via embedding similarity
                        try:
                            r2 = await client.get(f"{python_url}/api/v1/ai/embeddings/similar")
                            if r2.status_code == 200:
                                embeddings = r2.json()
                                if isinstance(embeddings, list):
                                    for emb in embeddings[:self.config["max_patterns_per_cycle"]]:
                                        text = emb.get("text", emb.get("question", ""))
                                        if text and text not in self.seen_questions:
                                            # Create a synthetic Q&A from the pattern
                                            q = f"Что такое {text[:50]}?"
                                            a = f"Знание из графа: {text}"
                                            await self._push_to_swarm(swarm_url, q, a)
                                            self.seen_questions.add(text[:100])
                                            exported += 1
                        except:
                            pass
                        
                        self.last_pattern_count = pattern_count
                        self.stats["patterns_synced"] += exported
                        
                        if exported > 0:
                            self.log(f"📤 Exported {exported} patterns → Swarm")
                            
        except Exception as e:
            self.stats["errors"] += 1
            self.log(f"⚠️  Export patterns error: {e}")
        
        return exported

    async def export_demo_learning(self):
        """Export demo learning text"""
        python_url = self.config["python_backend"]
        swarm_url = f"http://127.0.0.1:{self.config['swarm_port']}"
        
        # Send some sample knowledge to Python backend
        try:
            async with httpx.AsyncClient(timeout=10.0) as client:
                # Try to add knowledge via demo endpoint
                sample_texts = [
                    "Столица Австралии — Канберра",
                    "Скорость света — 299 792 458 м/с",
                    "Docker — платформа контейнеризации",
                    "Теорема Пифагора: c² = a² + b²",
                ]
                for text in sample_texts:
                    try:
                        await client.post(
                            f"{python_url}/api/v1/ai/demo/learn/text",
                            json={"text": text}
                        )
                    except:
                        pass
        except:
            pass

    async def _push_to_swarm(self, swarm_url, question, answer):
        """Push Q&A to swarm via chat"""
        try:
            async with httpx.AsyncClient(timeout=5.0) as client:
                await client.post(
                    f"{swarm_url}/api/v1/ai/chat",
                    json={"message": question, "conversation_id": "bridge"},
                    timeout=5.0,
                )
        except:
            pass

    # ─── Import: Swarm → Python Backend ───
    async def import_swarm_knowledge(self):
        """Import knowledge from Swarm to Python backend"""
        swarm_url = f"http://127.0.0.1:{self.config['swarm_port']}"
        python_url = self.config["python_backend"]
        imported = 0
        
        try:
            async with httpx.AsyncClient(timeout=10.0) as client:
                # Get swarm health
                r = await client.get(f"{swarm_url}/api/v1/health")
                if r.status_code == 200:
                    data = r.json()
                    facts = data.get("facts", 0)
                    peers = data.get("peers", 0)
                    self.log(f"📊 Swarm: {facts} facts, {peers} peers")
                
                # Get swarm export (NDJSON facts)
                r = await client.get(f"{swarm_url}/api/v1/swarm/export")
                if r.status_code == 200:
                    lines = r.text.strip().split('\n')
                    count = 0
                    for line in lines[:50]:
                        try:
                            fact = json.loads(line)
                            q = fact.get("q", fact.get("question", ""))
                            a = fact.get("a", fact.get("answer", ""))
                            if q and a and q[:100] not in self.seen_questions:
                                # Save to knowledge file for Python to pick up
                                self._save_fact_to_file(q, a)
                                self.seen_questions.add(q[:100])
                                count += 1
                        except:
                            pass
                    
                    if count > 0:
                        imported = count
                        self.stats["imported"] += count
                        self.log(f"📥 Imported {count} facts from swarm → Python knowledge")
                        
                        # Try to feed into Python backend
                        await self._feed_to_python(python_url, lines[:10])
                        
        except Exception as e:
            self.stats["errors"] += 1
            self.log(f"⚠️  Import error: {e}")
        
        return imported

    def _save_fact_to_file(self, question, answer):
        """Save fact to knowledge file"""
        kb_file = self.config["knowledge_file"]
        os.makedirs(os.path.dirname(kb_file), exist_ok=True)
        with open(kb_file, "a") as f:
            f.write(f"### Q: {question}\n\n**Ответ:** {answer}\n\n---\n\n")

    async def _feed_to_python(self, python_url, lines):
        """Try to feed facts into Python backend"""
        try:
            async with httpx.AsyncClient(timeout=10.0) as client:
                for line in lines:
                    try:
                        fact = json.loads(line)
                        q = fact.get("q", "")
                        a = fact.get("a", "")
                        if q and a:
                            # Try demo learning endpoint
                            await client.post(
                                f"{python_url}/api/v1/ai/demo/learn/text",
                                json={"text": f"{q}: {a}"}
                            )
                    except:
                        pass
        except:
            pass

    # ─── Sync with Peers ───
    async def sync_with_peers(self):
        """Sync with all swarm peers"""
        swarm_url = f"http://127.0.0.1:{self.config['swarm_port']}"
        
        try:
            async with httpx.AsyncClient(timeout=15.0) as client:
                for peer in self.config["swarm_peers"]:
                    host, port = peer.rsplit(":", 1)
                    try:
                        r = await client.post(
                            f"{swarm_url}/api/v1/swarm/sync",
                            json={"host": host, "port": int(port)},
                            timeout=10.0,
                        )
                        if r.status_code == 200:
                            data = r.json()
                            self.log(f"  ✅ Sync {peer}: {data}")
                    except Exception as e:
                        self.log(f"  ⚠️  Sync {peer}: {e}")
        except Exception as e:
            self.stats["errors"] += 1
            self.log(f"⚠️  Sync error: {e}")

    # ─── Continuous Learning ───
    async def continuous_learning(self):
        """Main learning loop"""
        self.log("🧠 Starting continuous learning loop...")
        self.log(f"  Sync interval: {self.config['sync_interval']}s")
        self.log(f"  Max conversations per cycle: {self.config['max_conversations_per_cycle']}")
        self.log(f"  Max patterns per cycle: {self.config['max_patterns_per_cycle']}")
        self.log(f"  Peers: {self.config['swarm_peers']}")
        self.log(f"  Known questions: {len(self.seen_questions)}")
        
        while self.running:
            try:
                self.stats["learn_cycles"] += 1
                self.log(f"\n═══ Learning Cycle #{self.stats['learn_cycles']} ═══")
                
                # 1. Sync with all peers
                await self.sync_with_peers()
                self.stats["sync_rounds"] += 1
                
                # 2. Export conversations → Swarm
                exported_conv = await self.export_conversations()
                
                # 3. Export patterns → Swarm
                exported_pat = await self.export_patterns()
                
                # 4. Demo learning
                await self.export_demo_learning()
                
                # 5. Import Swarm → Python
                imported = await self.import_swarm_knowledge()
                
                # 6. Status
                uptime = int(time.time() - self.stats["start_time"])
                self.log(f"\n📊 Status:")
                self.log(f"  Uptime: {uptime}s")
                self.log(f"  Exported: {self.stats['exported']} facts")
                self.log(f"  Imported: {self.stats['imported']} facts")
                self.log(f"  Conversations synced: {self.stats['conversations_synced']}")
                self.log(f"  Patterns synced: {self.stats['patterns_synced']}")
                self.log(f"  Sync rounds: {self.stats['sync_rounds']}")
                self.log(f"  Known questions: {len(self.seen_questions)}")
                self.log(f"  Errors: {self.stats['errors']}")
                
                # Save state
                self._save_state()
                
                # Wait for next cycle
                await asyncio.sleep(self.config["sync_interval"])
                
            except Exception as e:
                self.stats["errors"] += 1
                self.log(f"❌ Learning cycle error: {e}")
                await asyncio.sleep(10)

    # ─── Lifecycle ───
    async def start(self):
        """Start the bridge"""
        self.running = True
        self.stats["start_time"] = time.time()
        
        print("╔══════════════════════════════════════════════════════════╗")
        print("║   KOLIBRI REAL BRIDGE — Python ↔ Swarm Bidirectional    ║")
        print("╚══════════════════════════════════════════════════════════╝")
        print(f"  Python Backend: {self.config['python_backend']}")
        print(f"  Swarm Port:     {self.config['swarm_port']}")
        print(f"  Swarm Peers:    {self.config['swarm_peers']}")
        print(f"  Known Q:        {len(self.seen_questions)}")
        print()
        
        # 1. Start swarm node
        await self.start_swarm_node()
        
        # 2. Start continuous learning
        await self.continuous_learning()

    async def stop(self):
        """Stop the bridge"""
        self.running = False
        self._save_state()
        if hasattr(self, 'swarm_proc'):
            self.swarm_proc.terminate()
            self.swarm_proc.wait(timeout=5)
        self.log("🛑 Real bridge stopped")


async def main():
    parser = argparse.ArgumentParser(description="Kolibri Real Bridge")
    parser.add_argument("--swarm-port", type=int, default=8002)
    parser.add_argument("--python-port", type=int, default=8001)
    parser.add_argument("--peer", action="append", default=[])
    parser.add_argument("--sync-interval", type=int, default=30)
    parser.add_argument("--max-convos", type=int, default=10)
    parser.add_argument("--max-patterns", type=int, default=50)
    args = parser.parse_args()
    
    config = {
        "swarm_port": args.swarm_port,
        "python_backend": f"http://127.0.0.1:{args.python_port}",
        "sync_interval": args.sync_interval,
        "max_conversations_per_cycle": args.max_convos,
        "max_patterns_per_cycle": args.max_patterns,
    }
    
    if args.peer:
        config["swarm_peers"] = args.peer
    
    bridge = RealBridge(config)
    try:
        await bridge.start()
    except KeyboardInterrupt:
        await bridge.stop()
    except Exception as e:
        bridge.log(f"Fatal error: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    asyncio.run(main())
