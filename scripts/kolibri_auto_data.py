import os
import re
import time
import threading
import subprocess

def extract_numbers_from_file(filepath):
    numbers = []
    try:
        with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                found = re.findall(r'-?\d+', line)
                numbers.extend(map(int, found))
    except Exception:
        pass
    return numbers

def scan_project_for_numbers(root_dir):
    all_numbers = []
    for dirpath, _, filenames in os.walk(root_dir):
        for fname in filenames:
            if fname.endswith(('.py', '.c', '.h', '.md', '.txt', '.json', '.csv', '.ks')):
                fpath = os.path.join(dirpath, fname)
                nums = extract_numbers_from_file(fpath)
                all_numbers.extend(nums)
    return all_numbers

def send_examples_to_kolibri_sim(examples):
    # Пример: подаём пары (x, x+1) как обучающие примеры
    for x in examples:
        target = x + 1
        subprocess.run([
            './build/kolibri_sim', 'tick', '--seed', str(x)
        ], check=False)
        # Можно расширить: использовать API или другой CLI-интерфейс для передачи примеров

def background_data_collector(root_dir, interval_sec=60):
    while True:
        numbers = scan_project_for_numbers(root_dir)
        if numbers:
            send_examples_to_kolibri_sim(numbers)
        time.sleep(interval_sec)

def start_background_collector():
    root_dir = os.path.dirname(os.path.abspath(__file__))
    t = threading.Thread(target=background_data_collector, args=(root_dir,), daemon=True)
    t.start()

if __name__ == '__main__':
    print('Запуск фонового сбора данных для KolibriSim...')
    start_background_collector()
    while True:
        time.sleep(3600)
