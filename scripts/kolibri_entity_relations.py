import os
import re
import time
import threading
import ast
import subprocess
from collections import Counter, defaultdict

def extract_python_entity_relations(filepath):
    relations = []
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            tree = ast.parse(f.read(), filename=filepath)
        for node in ast.walk(tree):
            if isinstance(node, ast.FunctionDef):
                for call in ast.walk(node):
                    if isinstance(call, ast.Call) and isinstance(call.func, ast.Name):
                        relations.append((f'func:{node.name}', f'calls:{call.func.id}'))
            if isinstance(node, ast.ClassDef):
                for base in node.bases:
                    if isinstance(base, ast.Name):
                        relations.append((f'class:{node.name}', f'inherits:{base.id}'))
    except Exception:
        pass
    return relations

def scan_project_for_entity_relations(root_dir):
    all_relations = []
    for dirpath, _, filenames in os.walk(root_dir):
        for fname in filenames:
            if fname.endswith('.py'):
                fpath = os.path.join(dirpath, fname)
                rels = extract_python_entity_relations(fpath)
                all_relations.extend(rels)
    return all_relations

def write_examples_to_file(examples, out_path):
    with open(out_path, 'w', encoding='utf-8') as f:
        for inp, outp in examples:
            f.write(f'{inp}\t{outp}\n')

def send_relations_to_kolibri_sim(relations, examples_path='kolibri_entity_examples.txt'):
    # Сохраняем связи в файл (input, target)
    write_examples_to_file(relations, examples_path)
    # Можно интегрировать с KolibriSim через отдельный режим, который читает примеры из файла
    # Например: ./build/kolibri_sim learn --examples kolibri_entity_examples.txt
    try:
        subprocess.run([
            './build/kolibri_sim', 'tick', '--steps', str(len(relations))
        ], check=False)
    except Exception as e:
        print(f"[ERROR] Не удалось отправить связи: {e}")

def background_entity_relation_collector(root_dir, interval_sec=600):
    while True:
        relations = scan_project_for_entity_relations(root_dir)
        if relations:
            send_relations_to_kolibri_sim(relations)
        time.sleep(interval_sec)

def start_background_entity_collector():
    root_dir = os.path.dirname(os.path.abspath(__file__))
    t = threading.Thread(target=background_entity_relation_collector, args=(root_dir,), daemon=True)
    t.start()

if __name__ == '__main__':
    print('Запуск фонового анализа связей между сущностями для KolibriSim...')
    start_background_entity_collector()
    while True:
        time.sleep(3600)
