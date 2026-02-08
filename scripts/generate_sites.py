# Generates 100,000 synthetic sites for mass-scale training
import random

with open("seeds/internet_map_100k.txt", "w") as f:
    # 1. Real Seeds
    f.write("https://en.wikipedia.org/wiki/Cat\n")
    f.write("https://en.wikipedia.org/wiki/Dog\n")
    f.write("https://en.wikipedia.org/wiki/Artificial_intelligence\n")
    f.write("https://en.wikipedia.org/wiki/Kolmogorov_complexity\n")
    
    # 2. Synthetic Mass
    domains = ["wiki", "arxiv", "github", "news", "blog"]
    topics = ["science", "history", "art", "code", "future"]
    
    print("Generating 100,000 links...")
    for i in range(100000):
        d = random.choice(domains)
        t = random.choice(topics)
        f.write(f"http://{d}.org/{t}/{i}\n")

print("Done.")
