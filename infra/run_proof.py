import os
import subprocess

def run(cmd):
    #print(f"Running: {cmd}")
    res = subprocess.run(cmd, shell=True, capture_output=True, text=True)
    return res.stdout

# Setup
os.system("rm -rf data/proof && mkdir -p data/proof/nodes")
for i in range(1, 1001):
    with open(f"data/proof/nodes/node_{i}.dat", "wb") as f: pass

# 1. Generate Knowledge
print("Generating 100,000 synthetic events for the proof...")
subprocess.run("./build/kolibri_bulk_teach --count 100000 --out data/proof/source.dat", shell=True)

# 2. Shard to 1000 nodes
print("Sharding knowledge across 1000 nodes...")
subprocess.run("./build/kolibri_knowledge_relay --mode shard --source data/proof/source.dat --targets-dir data/proof/nodes --allow-events TEACH --target-key-inline kolibri-secret-key", shell=True)

# 3. Prove indexing
print("\n--- PROOF OF SHARDED INDEXING (1000 Nodes) ---")
found = 0
for i in [1, 2, 3, 500, 750, 1000]:
    out = run(f"./build/kolibri_inspect data/proof/nodes/node_{i}.dat")
    if "Block" in out:
        count = out.count("Block")
        print(f"Node #{i}: Indexed {count} entries.")

# Check all to be sure
total_files = 0
for i in range(1, 1001):
    if os.path.exists(f"data/proof/nodes/node_{i}.dat") and os.path.getsize(f"data/proof/nodes/node_{i}.dat") > 0:
        total_files += 1

print(f"\nTotal nodes with indexed knowledge: {total_files}/1000")
