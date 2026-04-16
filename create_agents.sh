#!/bin/bash
mkdir -p .kolibri/skills/src
mkdir -p .kolibri/skills/dist

SKILL_CREATOR="/Users/kolibri/.nvm/versions/node/v22.21.1/lib/node_modules/@google/gemini-cli/bundle/builtin/skill-creator/scripts/package_skill.cjs"

for file in agents/hive/*.md; do
    filename=$(basename "$file" .md)
    # lowercase and replace _ with -
    agent_name=$(echo "$filename" | tr '[:upper:]' '[:lower:]' | tr '_' '-')
    
    # Extract description
    desc_raw=$(grep -i "\*\*Focus\*\*:" "$file" | sed 's/.*Focus\*\*: *//' | tr -d '\n' | tr -d '\r' | sed 's/"/'\''/g')
    if [ -z "$desc_raw" ]; then
        desc_raw=$(grep -i "Focus:" "$file" | sed 's/.*Focus: *//' | tr -d '\n' | tr -d '\r' | sed 's/"/'\''/g')
    fi
    if [ -z "$desc_raw" ]; then
        desc_raw="Специализированный агент Kolibri AI: $agent_name"
    fi
    
    # Ensure description is not empty and single line
    skill_desc="Kolibri AI Hive-Mind Agent: $desc_raw Use when you need to act as $agent_name."
    
    skill_dir=".kolibri/skills/src/$agent_name"
    mkdir -p "$skill_dir"
    
    cat << YAMLEOF > "$skill_dir/SKILL.md"
---
name: $agent_name
description: "$skill_desc"
---
YAMLEOF
    
    cat "$file" >> "$skill_dir/SKILL.md"
    
    node "$SKILL_CREATOR" "$skill_dir" .kolibri/skills/dist > /dev/null
    echo "Packaged $agent_name"
done

echo "Installing skills into workspace..."
gemini skills install .kolibri/skills/dist/*.skill --scope workspace
echo "Done!"
