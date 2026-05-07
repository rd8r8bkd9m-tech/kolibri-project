import requests
import json

def test_nonlinear_logic():
    url = "http://127.0.0.1:8000/solve/hybrid"
    
    # Генерируем данные для функции: y = (x * 37) ^ 0xFF (имитация простого шифрования)
    inputs = list(range(1, 21))
    outputs = [((x * 37) ^ 0xFF) & 0xFFFF for x in inputs]
    
    payload = {
        "inputs": inputs,
        "outputs": outputs,
        "generations": 500
    }
    
    print(f"[CLIENT] Отправка задачи на взлом логики ({len(inputs)} пар)...")
    response = requests.post(url, json=payload)
    
    if response.status_code == 200:
        print("[SUCCESS] Kolibri AI справился с нелинейной задачей!")
        print(response.json())
    else:
        print(f"[ERROR] {response.status_code}: {response.text}")

if __name__ == "__main__":
    test_nonlinear_logic()
