#!/bin/bash
SKILL_CREATOR="/Users/kolibri/.nvm/versions/node/v22.21.1/lib/node_modules/@google/gemini-cli/bundle/builtin/skill-creator/scripts/package_skill.cjs"

# Текст Конституции, который будет добавлен к каждому агенту
CONSTITUTION=$(cat << 'INNER_EOF'

---
## Kolibri AI Hive-Mind Constitution

### 1. Unified Project Concept
Kolibri AI — это единый монорепозиторий (`UI <-> WASM <-> Python <-> C-Core`). Все агенты работают над улучшением этого единого организма.

### 2. Parallel Execution Protocol
- **Heavy Tasks:** Компиляция `/core`, обучение ИИ и фаззинг выполняются на удаленном сервере `ubuntu-home-wan`.
- **Light Tasks:** UI, доки и мелкие правки — локально.
- **Sync:** Все изменения синхронизируются через `infra/remote/sync.sh` перед запуском удаленных задач.

### 3. Operational Rules
- **No Interaction:** Решайте задачи автономно. Запрещено спрашивать пользователя, если решение можно найти в коде или документации.
- **Zero Drift:** Код, тесты и документация должны быть синхронны.
- **Single Source of Truth:** `docs/plans/agent-squad-plan.md` и `GEMINI.md`.
INNER_EOF
)

for file in agents/hive/*.md; do
    filename=$(basename "$file" .md)
    agent_name=$(echo "$filename" | tr '[:upper:]' '[:lower:]' | tr '_' '-')
    skill_dir=".kolibri/skills/src/$agent_name"
    
    # Добавляем Конституцию в конец SKILL.md
    echo "$CONSTITUTION" >> "$skill_dir/SKILL.md"
    
    # Перепаковываем скилл
    node "$SKILL_CREATOR" "$skill_dir" .kolibri/skills/dist > /dev/null
    echo "Re-packaged $agent_name with Constitution"
done

echo "Re-installing skills into workspace..."
for skill in .kolibri/skills/dist/*.skill; do
    gemini skills install "$skill" --scope workspace --consent > /dev/null 2>&1
    echo "Installed $skill"
done
echo "All 21 agents have been updated with the full Hive-Mind Constitution!"
