import os
import json

def generate_knowledge():
    knowledge = []
    # Проекты пользователя
    projects_dirs = ['.']

    print(f"Scanning workspace...")

    for root_dir in projects_dirs:
        for root, dirs, files in os.walk(root_dir):
            if 'node_modules' in root or '.git' in root or 'dist' in root:
                continue

            for file in files:
                if file.endswith(('.c', '.h', '.ts', '.tsx', '.js', '.md', '.json', '.py')):
                    full_path = os.path.join(root, file)
                    try:
                        with open(full_path, 'r', encoding='utf-8') as f:
                            # Берем первые 200 символов как "смысл" файла
                            content = f.read(200).replace('\n', ' ').replace('"', "'")
                            knowledge.append({
                                "premise": full_path.replace('./', ''),
                                "conclusion": f"Файл {file}. Содержит: {content}..."
                            })
                    except:
                        continue

    # Сохраняем в public для доступа из браузера
    with open('web/public/knowledge.json', 'w', encoding='utf-8') as f:
        json.dump(knowledge, f, ensure_ascii=False, indent=2)

    print(f"Generated knowledge.json with {len(knowledge)} entries.")

if __name__ == "__main__":
    generate_knowledge()
