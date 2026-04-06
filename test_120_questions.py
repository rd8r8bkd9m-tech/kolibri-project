#!/usr/bin/env python3
"""
test_120_questions.py — test how swarm nodes answer 120 questions
"""

import json
import httpx
import sys
import time
from datetime import datetime

NODES = [
    {"name": "Mac Hybrid", "url": "http://localhost:8003/api/v1/ai/chat"},
    {"name": "Node 2 Swarm", "url": "http://localhost:8002/api/v1/ai/chat"},
    {"name": "Node 1 Swarm", "url": "http://217.60.249.157:8001/api/v1/ai/chat"},
]

def test_questions(node, questions):
    """Send questions to a node and collect answers"""
    results = []
    total = len(questions)
    
    print(f"\n{'='*60}")
    print(f"📡 Testing: {node['name']} ({node['url']})")
    print(f"{'='*60}")
    
    async def _run():
        async with httpx.AsyncClient(timeout=10.0) as client:
            for i, q in enumerate(questions, 1):
                try:
                    r = await client.post(
                        node["url"],
                        json={"message": q, "conversation_id": f"test_120_q{i}"},
                        headers={"Content-Type": "application/json"}
                    )
                    if r.status_code == 200:
                        data = r.json()
                        answer = data.get("response", "")
                        source = data.get("source", data.get("method", "?"))
                        confidence = data.get("confidence", 0)
                        print(f"  Q{i:3d}. {q[:50]:50s} → [{source}] {answer[:40]}")
                        results.append({
                            "q": q, "answer": answer, 
                            "source": source, "confidence": confidence
                        })
                    else:
                        print(f"  Q{i:3d}. {q[:50]:50s} → HTTP {r.status_code}")
                        results.append({"q": q, "answer": "", "source": f"HTTP {r.status_code}"})
                except Exception as e:
                    print(f"  Q{i:3d}. {q[:50]:50s} → ERR: {str(e)[:30]}")
                    results.append({"q": q, "answer": "", "source": "ERROR"})
                
                if i % 10 == 0:
                    print(f"  ... {i}/{total} done")
                
                # Small delay to avoid rate limiting
                await asyncio.sleep(0.3)
    
    import asyncio
    asyncio.run(_run())
    
    return results


def main():
    # Read questions
    with open("/tmp/120_questions.txt") as f:
        questions = [line.strip() for line in f if line.strip()]
    
    print(f"📋 Loaded {len(questions)} questions")
    print(f"📊 Nodes to test: {len(NODES)}")
    
    for node in NODES:
        results = test_questions(node, questions)
        
        # Stats
        answered = [r for r in results if r["answer"] and len(r["answer"]) > 5]
        errors = [r for r in results if not r["answer"] or r["source"] == "ERROR"]
        high_conf = [r for r in answered if r.get("confidence", 0) > 0.7]
        
        print(f"\n{'='*60}")
        print(f"📊 {node['name']} Results:")
        print(f"  Total:      {len(results)}")
        print(f"  Answered:   {len(answered)}/{len(results)} ({len(answered)*100//len(results)}%)")
        print(f"  High conf:  {len(high_conf)}/{len(answered)}")
        print(f"  Errors:     {len(errors)}/{len(results)}")
        print(f"{'='*60}")
        
        # Save results
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        safe_name = node["name"].replace(" ", "_")
        outfile = f"/tmp/test_120_{safe_name}_{ts}.json"
        with open(outfile, "w") as f:
            json.dump({"node": node["name"], "results": results}, f, ensure_ascii=False, indent=2)
        print(f"💾 Saved to {outfile}")
        print()


if __name__ == "__main__":
    main()
