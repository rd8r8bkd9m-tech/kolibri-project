#!/usr/bin/env python3
"""
Генерирует конфиг для 50 узлов свопа.
Каждый узел имеет 3-4 пира для P2P синхронизации.
"""
import json
from pathlib import Path

def generate_nodes_config(count: int = 50) -> dict:
    """Генерирует конфигурацию для N узлов."""
    
    nodes = []
    base_port = 8001
    
    for i in range(count):
        node_name = f"node_{chr(97 + (i % 26))}{i // 26 if i >= 26 else ''}"
        port = base_port + i
        
        # Каждый узел имеет 3-4 соседей (кольцевая топология + несколько случайных)
        peers = []
        
        # Соседи в кольце
        prev_idx = (i - 1) % count
        next_idx = (i + 1) % count
        
        peers.append(f"node_{chr(97 + (prev_idx % 26))}{prev_idx // 26 if prev_idx >= 26 else ''}")
        peers.append(f"node_{chr(97 + (next_idx % 26))}{next_idx // 26 if next_idx >= 26 else ''}")
        
        # Добавляем случайных соседей (несколько шагов вперёд/назад)
        for offset in [5, -5, 10]:
            neighbor_idx = (i + offset) % count
            neighbor_name = f"node_{chr(97 + (neighbor_idx % 26))}{neighbor_idx // 26 if neighbor_idx >= 26 else ''}"
            if neighbor_name not in peers and neighbor_name != node_name:
                peers.append(neighbor_name)
        
        nodes.append({
            "name": node_name,
            "port": port,
            "genome": f"build/knowledge/approved/{node_name}/knowledge_genome.dat",
            "receive_dir": f"build/swarm/inbox/{node_name}",
            "hmac_key_inline": "kolibri-knowledge",
            "peers": peers[:4],  # Максимум 4 пира
        })
    
    return {"nodes": nodes}


if __name__ == "__main__":
    config = generate_nodes_config(50)
    
    # Сохраняем
    nodes_path = Path(__file__).parent / "nodes.json"
    nodes_path.write_text(json.dumps(config, indent=2, ensure_ascii=False), encoding="utf-8")
    
    print(f"✅ Сгенерирован конфиг для {len(config['nodes'])} узлов")
    print(f"📄 Сохранён в {nodes_path}")
    
    # Выводим первые 5 узлов
    for node in config['nodes'][:5]:
        print(f"  {node['name']:20} port={node['port']:5} peers={node['peers']}")
    print(f"  ...")
