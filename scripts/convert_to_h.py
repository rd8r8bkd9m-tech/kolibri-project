import json

with open('web/public/knowledge.json', 'r', encoding='utf-8') as f:
    data = json.load(f)

with open('core/knowledge_data.h', 'w', encoding='utf-8') as f:
    f.write('typedef struct { const char* p; const char* c; } KData;\n')
    f.write('static const KData KNOWLEDGE_BASE[] = {\n')

    # Берем 1000 записей для полноценного теста
    for item in data[:1000]:
        # Экранируем спецсимволы для C-строк
        p = item['premise'].replace('\\', '\\\\').replace('"', '\\"')
        c = item['conclusion'].replace('\\', '\\\\').replace('"', '\\"').replace('\n', ' ')
        f.write(f'  {{"{p}", "{c}"}},\n')

    f.write('  {NULL, NULL}\n};\n')

print("✅ Готово: core/knowledge_data.h (1000 файлов проиндексировано)")
