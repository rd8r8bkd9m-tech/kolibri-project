import os
import subprocess
import concurrent.futures
import time

QUEUE_DIR = os.getenv("QUEUE_DIR", "data/queue")
BASE_DIR = os.getenv("BASE_DIR", "data/swarm_proof")
NODE_COUNT = int(os.getenv("NODE_COUNT", "1000"))
GENS_PER_SITE = int(os.getenv("GENS_PER_SITE", "50"))
MAX_WORKERS = int(os.getenv("WORKERS", "32"))
LIMIT_JOBS = int(os.getenv("LIMIT_JOBS", "0"))

def process_job(job_path):
    job_id = os.path.basename(job_path)
    shard_file = f"{BASE_DIR}/shards/shard_{job_id}.dat"
    log_file = f"{BASE_DIR}/logs/job_{job_id}.log"
    
    cmd = (
        f"cat {job_path} | ./build/kolibri_fast_parser | "
        f"KOLIBRI_GENOME_PATH={shard_file} KOLIBRI_GENS={GENS_PER_SITE} "
        f"./build/kolibri_ingest > {log_file} 2>&1"
    )
    subprocess.run(cmd, shell=True, check=False)
    return job_id

def main():
    os.makedirs(f"{BASE_DIR}/shards", exist_ok=True)
    os.makedirs(f"{BASE_DIR}/nodes", exist_ok=True)
    os.makedirs(f"{BASE_DIR}/logs", exist_ok=True)
    
    for i in range(1, NODE_COUNT + 1):
        with open(f"{BASE_DIR}/nodes/node_{i}.dat", "wb"): pass
        
    jobs = [os.path.join(QUEUE_DIR, f) for f in os.listdir(QUEUE_DIR) if f.startswith("job_")]
    jobs.sort()
    if LIMIT_JOBS > 0:
        jobs = jobs[:LIMIT_JOBS]
    total = len(jobs)
    print(
        f"Starting Swarm Ingest for {total} jobs... "
        f"(workers={MAX_WORKERS}, gens={GENS_PER_SITE})"
    )
    
    start_time = time.time()
    completed = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=MAX_WORKERS) as executor:
        futures = [executor.submit(process_job, job) for job in jobs]
        for future in concurrent.futures.as_completed(futures):
            completed += 1
            if completed % 10 == 0 or completed == total:
                elapsed = time.time() - start_time
                rate = completed / elapsed if elapsed > 0 else 0.0
                print(
                    f"   Progress: {completed}/{total} jobs completed "
                    f"({rate:.2f} jobs/sec)",
                    flush=True,
                )
    
    print(f"\nDistributing knowledge across {NODE_COUNT} nodes...")
    shards = [os.path.join(f"{BASE_DIR}/shards", f) for f in os.listdir(f"{BASE_DIR}/shards") if f.endswith(".dat")]
    relay_start = time.time()
    for s in shards:
        subprocess.run(f"./build/kolibri_knowledge_relay --mode shard --source {s} --targets-dir {BASE_DIR}/nodes --allow-events DEEP_L --target-key-inline kolibri-secret-key > /dev/null 2>&1", shell=True)
    relay_elapsed = time.time() - relay_start
    
    print("\n--- Final Proof (Node #123) ---")
    res = subprocess.run(f"./build/kolibri_inspect {BASE_DIR}/nodes/node_123.dat", shell=True, capture_output=True, text=True)
    print(res.stdout[:500])
    
    non_empty = sum(1 for f in os.listdir(f"{BASE_DIR}/nodes") if os.path.getsize(os.path.join(f"{BASE_DIR}/nodes", f)) > 0)
    total_time = time.time() - start_time
    shards_size = sum(
        os.path.getsize(os.path.join(f"{BASE_DIR}/shards", f))
        for f in os.listdir(f"{BASE_DIR}/shards")
        if f.endswith(".dat")
    )
    print(f"Total non-empty nodes: {non_empty}/{NODE_COUNT}")
    print(f"Total shards size: {shards_size / (1024 * 1024):.2f} MB")
    print(f"Total time: {total_time:.1f}s (relay: {relay_elapsed:.1f}s)")

if __name__ == "__main__":
    main()
