import urllib.request
import json
import time

URL = "http://localhost:8001/api/v1/ai/chat"

questions = [
    "Если А > Б и Б > В, то А > В?",
    "Все люди смертны. Сократ — человек. Какой вывод?",
    "Если идет дождь, то земля мокрая. Земля не мокрая. Какой вывод?",
    "Что тяжелее: килограмм пуха или килограмм свинца?",
    "У Маши 3 яблока, у Пети в 2 раза больше. Сколько яблок у них вместе?",
    "Если у квадрата сторона 4, какая площадь?",
    "Как продолжить последовательность: 2, 4, 8, 16, ...?",
    "Что будет, если соединить кислоту и щелочь?",
    "Объясни причину смены времен года",
    "Если 5 машин шьют 5 платьев 5 минут, за сколько 100 машин сошьют 100 платьев?",
    "У отца Мэри есть 5 дочерей: Нана, Нене, Нини, Ноно. Как зовут пятую?",
    "В комнате было 10 свечей, 3 задули. Сколько свечей осталось?",
    "Какой город является столицей Японии?",
    "Что такое фотосинтез в одном предложении?",
    "Сколько будет 15% от 200?",
    "Закон сохранения энергии это...",
    "Если завтра был вчера, то сегодня это что?",
    "У кошки 3 котенка: Черныш, Рыжик и Беляк. Как зовут мать?",
    "Что общего у Луны и яблока Ньютона?",
    "Почему самолеты летают?",
    "Как работает блокчейн простыми словами?",
    "Что такое энтропия?",
    "В чем смысл жизни по версии 42?",
    "Кто написал 'Преступление и наказание'?",
    "Какая планета самая большая в Солнечной системе?",
    "Скорость звука в воздухе выше или ниже скорости света?",
    "Из чего состоит вода?",
    "Что такое ДНК?",
    "Почему небо голубое?",
    "Как работает GPS?",
    "Что такое инфляция?",
    "Кто открыл Америку?",
    "Какое число идет после миллиона?",
    "Что такое квантовая запутанность?",
    "Как работает нейросеть?",
    "Что такое черная дыра?",
    "В чем разница между вирусом и бактерией?",
    "Почему лед плавает в воде?",
    "Как работает термос?",
    "Что такое точка кипения?"
]

def test_kolibri():
    print(f"=== Kolibri AI Deep Test (40 Questions) ===")
    results = []
    
    for i, q in enumerate(questions):
        print(f"[{i+1}/40] Testing: {q}")
        try:
            payload = json.dumps({
                "message": q,
                "temperature": 0.7,
                "profile": "balanced"
            }).encode('utf-8')
            
            req = urllib.request.Request(URL, data=payload, headers={'Content-Type': 'application/json'})
            
            start = time.time()
            with urllib.request.urlopen(req, timeout=15) as response:
                end = time.time()
                data = json.loads(response.read().decode('utf-8'))
                
                print(f"  ✅ Method: {data.get('method')} | Conf: {data.get('confidence')} | Time: {data.get('duration_ms')}ms")
                results.append({
                    "q": q,
                    "status": "OK",
                    "method": data.get("method"),
                    "confidence": data.get("confidence"),
                    "cognitive": data.get("cognitive"),
                    "formula": data.get("formula_data"),
                    "duration": data.get("duration_ms")
                })
        except Exception as e:
            print(f"  ⚠️ Exception: {e}")
            results.append({"q": q, "status": "EXCEPTION"})
        
        time.sleep(0.1)

    print("\n=== FINAL REPORT ===")
    success = [r for r in results if r.get("status") == "OK"]
    print(f"Total: {len(questions)}")
    print(f"Success: {len(success)}")
    
    avg_conf = sum(r.get("confidence", 0) for r in success) / len(success) if success else 0
    print(f"Average Confidence: {avg_conf:.4f}")
    
    methods = {}
    for r in success:
        m = r.get("method", "unknown")
        methods[m] = methods.get(m, 0) + 1
    
    print("Methods used:")
    for m, count in methods.items():
        print(f"  - {m}: {count}")

if __name__ == "__main__":
    test_kolibri()
