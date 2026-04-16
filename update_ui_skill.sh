#!/bin/bash
SKILL_CREATOR="/Users/kolibri/.nvm/versions/node/v22.21.1/lib/node_modules/@google/gemini-cli/bundle/builtin/skill-creator/scripts/package_skill.cjs"

# Обновляем UI_INTEGRATOR.md
sed -i '' 's/- Компонентный подход.*/- Компонентный подход (Mantine First): Строго использовать компоненты Mantine UI v7 (AppShell, Stack, Group, Style Props). Написание кастомного Vanilla CSS запрещено./g' agents/hive/UI_INTEGRATOR.md

# Пересобираем агентов
for file in agents/hive/UI_INTEGRATOR.md agents/hive/UX_VISIONARY.md; do
    filename=$(basename "$file" .md)
    agent_name=$(echo "$filename" | tr '[:upper:]' '[:lower:]' | tr '_' '-')
    skill_dir=".kolibri/skills/src/$agent_name"
    
    cp "$file" "$skill_dir/SKILL.md.tmp"
    
    # Добавляем YAML шапку
    desc_raw=$(grep -i "\*\*Focus\*\*:" "$file" | sed 's/.*Focus\*\*: *//' | tr -d '\n' | tr -d '\r' | sed 's/"/'\''/g')
    skill_desc="Kolibri AI Hive-Mind Agent: $desc_raw Use when you need to act as $agent_name."
    
    cat << YAMLEOF > "$skill_dir/SKILL.md"
---
name: $agent_name
description: "$skill_desc"
---
YAMLEOF
    
    cat "$skill_dir/SKILL.md.tmp" >> "$skill_dir/SKILL.md"
    rm "$skill_dir/SKILL.md.tmp"
    
    # Добавляем Конституцию
    cat << 'INNER_EOF' >> "$skill_dir/SKILL.md"

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

    node "$SKILL_CREATOR" "$skill_dir" .kolibri/skills/dist > /dev/null
    gemini skills install ".kolibri/skills/dist/$agent_name.skill" --scope workspace --consent > /dev/null 2>&1
    echo "Updated agent: $agent_name"
done
