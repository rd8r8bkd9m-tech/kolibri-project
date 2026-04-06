"""
kolibri_knowledge_bridge.py — runs on Node 2
Bridges Python backend (kolibriai.ru) with Kolibri Swarm network.

What it does:
1. Continuously exports knowledge from Python backend → Swarm
2. Imports knowledge from Swarm → Python backend
3. Bidirectional sync with all swarm nodes
4. Learns from internet queries + other nodes

Usage: python3 kolibri_knowledge_bridge.py --swarm-port 8002 --python-port 8001
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

# ─── Configuration ───
DEFAULT_CONFIG = {
    "python_backend": "http://127.0.0.1:8001",
    "swarm_port": 8002,
    "swarm_peers": [
        "217.60.249.157:8001",  # Node 1
    ],
    "sync_interval": 30,       # seconds between sync rounds
    "export_batch": 20,        # facts per export batch
    "knowledge_file": "/srv/kolibri/repo/knowledge/swarm_sync.md",
    "log_file": "/tmp/kolibri_bridge.log",
}

class KolibriBridge:
    """Bidirectional bridge between Python backend and Swarm network"""
    
    def __init__(self, config=None):
        self.config = {**DEFAULT_CONFIG, **(config or {})}
        self.running = False
        self.stats = {
            "exported": 0,       # Python → Swarm
            "imported": 0,       # Swarm → Python
            "sync_rounds": 0,
            "learn_cycles": 0,
            "errors": 0,
            "start_time": None,
        }
        self.seen_facts = set()  # Avoid duplicates

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
        """Start kolibri_swarm binary on this server"""
        swarm_binary = os.path.join(os.path.expanduser("~"), "kolibri", "kolibri_swarm")
        kb_file = os.path.join(os.path.expanduser("~"), "kolibri", "knowledge", "knowledge_base.md")
        
        if not os.path.exists(swarm_binary):
            self.log(f"⚠️  Swarm binary not found at {swarm_binary}")
            return False
        
        # Kill existing
        subprocess.run(["pkill", "-f", f"kolibri_swarm {self.config['swarm_port']}"],
                      capture_output=True)
        await asyncio.sleep(1)
        
        # Build peer args
        peers = []
        for p in self.config["swarm_peers"]:
            peers.extend(["--peer", p])
        
        cmd = [swarm_binary, str(self.config["swarm_port"])] + peers
        
        self.log(f"🐝 Starting swarm :{self.config['swarm_port']} peers={self.config['swarm_peers']}")
        
        log_path = "/tmp/kolibri_swarm_node2.log"
        self.swarm_proc = subprocess.Popen(
            cmd,
            stdout=open(log_path, "w"),
            stderr=subprocess.STDOUT,
            cwd=os.path.dirname(swarm_binary),
        )
        
        # Wait for startup
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
    async def export_python_knowledge(self):
        """Export knowledge from Python backend to Swarm network"""
        swarm_url = f"http://127.0.0.1:{self.config['swarm_port']}"
        python_url = self.config["python_backend"]
        
        exported = 0
        
        try:
            async with httpx.AsyncClient(timeout=30.0) as client:
                # 1. Get knowledge analytics from Python backend
                r = await client.get(f"{python_url}/api/v1/ai/knowledge/analytics")
                if r.status_code == 200:
                    data = r.json()
                    # Extract knowledge items
                    items = []
                    
                    # Try different response formats
                    if isinstance(data, dict):
                        items = (data.get("knowledge", []) or 
                                 data.get("facts", []) or
                                 data.get("patterns", []) or
                                 data.get("documents", []))
                    
                    if isinstance(items, list) and items:
                        batch = items[:self.config["export_batch"]]
                        for item in batch:
                            q = item.get("question", item.get("q", item.get("text", "")))
                            a = item.get("answer", item.get("a", ""))
                            if q and a and q not in self.seen_facts:
                                await self._push_to_swarm(swarm_url, q, a)
                                self.seen_facts.add(q)
                                exported += 1
                        
                        if exported > 0:
                            self.stats["exported"] += exported
                            self.log(f"📤 Exported {exported} facts: Python → Swarm")
                
                # 2. Export conversation knowledge
                if exported == 0:
                    # Fallback: extract from recent conversations
                    r = await client.get(
                        f"{python_url}/api/v1/ai/conversations",
                        params={"limit": 5}
                    )
                    if r.status_code == 200:
                        convos = r.json()
                        if isinstance(convos, list):
                            for convo in convos[:3]:
                                turns = convo.get("turns", [])
                                for turn in turns[:2]:
                                    q = turn.get("user", turn.get("question", ""))
                                    a = turn.get("assistant", turn.get("answer", ""))
                                    if q and a and q not in self.seen_facts:
                                        await self._push_to_swarm(swarm_url, q, a)
                                        self.seen_facts.add(q)
                                        exported += 1
                                        
        except Exception as e:
            self.stats["errors"] += 1
            self.log(f"⚠️  Export error: {e}")
        
        return exported

    async def _push_to_swarm(self, swarm_url: str, question: str, answer: str):
        """Push a Q&A fact to swarm via chat endpoint"""
        try:
            async with httpx.AsyncClient(timeout=5.0) as client:
                # Swarm learns from chat interactions
                await client.post(
                    f"{swarm_url}/api/v1/ai/chat",
                    json={"message": question, "conversation_id": "bridge_sync"},
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
                # 1. Get swarm health (fact count)
                r = await client.get(f"{swarm_url}/api/v1/health")
                if r.status_code == 200:
                    data = r.json()
                    facts_count = data.get("facts", 0)
                    peers_count = data.get("peers", 0)
                    
                    self.log(f"📊 Swarm status: {facts_count} facts, {peers_count} peers")
                    
                    # Save to knowledge file for Python backend to pick up
                    kb_file = self.config["knowledge_file"]
                    os.makedirs(os.path.dirname(kb_file), exist_ok=True)
                    
                    with open(kb_file, "w") as f:
                        f.write(f"# Swarm Knowledge Sync\n")
                        f.write(f"# Updated: {datetime.now().isoformat()}\n")
                        f.write(f"# Facts: {facts_count}, Peers: {peers_count}\n\n")
                    
                    imported = facts_count
                    self.stats["imported"] = imported
                
                # 2. Push swarm facts to Python backend knowledge store
                # Use the /api/v1/ai/knowledge/add endpoint if available
                try:
                    r = await client.get(f"{swarm_url}/api/v1/swarm/export")
                    if r.status_code == 200:
                        lines = r.text.strip().split('\n')
                        count = 0
                        for line in lines[:50]:  # Batch limit
                            try:
                                fact = json.loads(line)
                                q = fact.get("q", fact.get("question", ""))
                                a = fact.get("a", fact.get("answer", ""))
                                if q and a and q not in self.seen_facts:
                                    # Store locally
                                    with open(kb_file, "a") as f:
                                        f.write(f"### Q: {q}\n\n**Ответ:** {a}\n\n---\n\n")
                                    self.seen_facts.add(q)
                                    count += 1
                            except:
                                pass
                        
                        if count > 0:
                            self.log(f"📥 Imported {count} new facts from swarm")
                except:
                    pass
                    
        except Exception as e:
            self.stats["errors"] += 1
            self.log(f"⚠️  Import error: {e}")
        
        return imported

    # ─── Sync with Peers ───
    async def sync_with_peers(self):
        """Explicit sync with all swarm peers"""
        swarm_url = f"http://127.0.0.1:{self.config['swarm_port']}"
        
        try:
            async with httpx.AsyncClient(timeout=15.0) as client:
                # Trigger sync from each peer
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
        """Main learning loop — runs forever"""
        self.log("🧠 Starting continuous learning loop...")
        self.log(f"  Sync interval: {self.config['sync_interval']}s")
        self.log(f"  Export batch: {self.config['export_batch']} facts")
        self.log(f"  Peers: {self.config['swarm_peers']}")
        
        while self.running:
            try:
                self.stats["learn_cycles"] += 1
                self.log(f"\n═══ Learning Cycle #{self.stats['learn_cycles']} ═══")
                
                # 1. Sync with all peers
                await self.sync_with_peers()
                self.stats["sync_rounds"] += 1
                
                # 2. Export Python knowledge → Swarm
                exported = await self.export_python_knowledge()
                
                # 3. Import Swarm knowledge → Python
                imported = await self.import_swarm_knowledge()
                
                # 4. Status report
                uptime = int(time.time() - self.stats["start_time"])
                self.log(f"\n📊 Status:")
                self.log(f"  Uptime: {uptime}s")
                self.log(f"  Exported: {self.stats['exported']} facts")
                self.log(f"  Imported: {self.stats['imported']} facts")
                self.log(f"  Sync rounds: {self.stats['sync_rounds']}")
                self.log(f"  Errors: {self.stats['errors']}")
                self.log(f"  Known facts: {len(self.seen_facts)}")
                
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
        print("║   KOLIBRI SWARM BRIDGE — Bidirectional Knowledge Sync   ║")
        print("╚══════════════════════════════════════════════════════════╝")
        print(f"  Python Backend: {self.config['python_backend']}")
        print(f"  Swarm Port:     {self.config['swarm_port']}")
        print(f"  Swarm Peers:    {self.config['swarm_peers']}")
        print(f"  Sync Interval:  {self.config['sync_interval']}s")
        print()
        
        # 1. Start swarm node
        await self.start_swarm_node()
        
        # 2. Start continuous learning
        await self.continuous_learning()

    async def stop(self):
        """Stop the bridge"""
        self.running = False
        if hasattr(self, 'swarm_proc'):
            self.swarm_proc.terminate()
            self.swarm_proc.wait(timeout=5)
        self.log("🛑 Swarm bridge stopped")


async def main():
    parser = argparse.ArgumentParser(description="Kolibri Swarm Bridge")
    parser.add_argument("--swarm-port", type=int, default=8002)
    parser.add_argument("--python-port", type=int, default=8001)
    parser.add_argument("--peer", action="append", default=[])
    parser.add_argument("--sync-interval", type=int, default=30)
    args = parser.parse_args()
    
    config = {
        "swarm_port": args.swarm_port,
        "python_backend": f"http://127.0.0.1:{args.python_port}",
        "sync_interval": args.sync_interval,
    }
    
    if args.peer:
        config["swarm_peers"] = args.peer
    
    bridge = KolibriBridge(config)
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
