import os
import re

def fnv1a_32(text):
    hash_val = 2166136261
    for char in text.encode('utf-8'):
        hash_val ^= char
        hash_val *= 16777619
        hash_val &= 0xFFFFFFFF
    return hash_val

def kf_hash_from_text(text):
    # Копия алгоритма из formula.c (если там fnv1a)
    # Судя по коду, там может быть знаковость
    h = fnv1a_32(text)
    if h > 0x7FFFFFFF:
        return h - 0x100000000
    return h

target = 43552498
path = "docs/wikipedia"

for filename in os.listdir(path):
    if filename.endswith(".md"):
        with open(os.path.join(path, filename), 'r', encoding='utf-8') as f:
            content = f.read()
            words = re.findall(r'\w+', content)
            for word in words:
                if kf_hash_from_text(word.lower()) == target:
                    print(f"Найдено совпадение в {filename}: {word}")
