
import os
import re
import time
import threading
import tokenize
import keyword
import subprocess
from collections import Counter

def extract_code_features(filepath):
    features = Counter()
    try:
        if filepath.endswith('.py'):
            with open(filepath, 'rb') as f:
                tokens = tokenize.tokenize(f.readline)
                for toknum, tokval, *_ in tokens:
                    if toknum == 1:  # NAME
                        if keyword.iskeyword(tokval):
                            features['kw_' + tokval] += 1
                        else:
                            features['id_' + tokval] += 1
        else:
            with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
                text = f.read()
                # Простая разбивка по словам для C/Markdown/текста
                words = re.findall(r'\b\w+\b', text)
                for word in words:
                    features['word_' + word] += 1
    except Exception:
        pass
    return features

def scan_project_for_code_features(root_dir):
    all_features = Counter()
    for dirpath, _, filenames in os.walk(root_dir):
        for fname in filenames:
            if fname.endswith(('.py', '.c', '.h', '.md', '.txt', '.json', '.csv', '.ks')):
                fpath = os.path.join(dirpath, fname)
                feats = extract_code_features(fpath)
                all_features.update(feats)
    return all_features

def send_features_to_kolibri_sim(features):
    # Интеграция: подаём признаки как обучающие примеры через kolibri_sim CLI
    for feat, count in features.items():
        input_val = abs(hash(feat)) % (2**31)
        target_val = count
        # Используем seed=input_val, steps=target_val для обучения
        try:
            subprocess.run([
                './build/kolibri_sim', 'tick', '--seed', str(input_val), '--steps', str(target_val)
            ], check=False)
        except Exception as e:
            print(f"[ERROR] Не удалось отправить признак {feat}: {e}")

def background_code_feature_collector(root_dir, interval_sec=300):
    while True:
        features = scan_project_for_code_features(root_dir)
        if features:
            send_features_to_kolibri_sim(features)
        time.sleep(interval_sec)

def start_background_code_collector():
    root_dir = os.path.dirname(os.path.abspath(__file__))
    t = threading.Thread(target=background_code_feature_collector, args=(root_dir,), daemon=True)
    t.start()

if __name__ == '__main__':
    print('Запуск фонового анализа кода и текста для KolibriSim...')
    start_background_code_collector()
    while True:
        time.sleep(3600)
