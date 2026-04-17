import re

with open("web/src/App.tsx", "r") as f:
    content = f.read()

import_old = """import { KolibriBridge, KolibriHealth, KolibriBridgeResult } from "./lib/kolibriBridge";"""
import_new = """import { KolibriBridge, KolibriHealth, KolibriBridgeResult } from "./lib/kolibriBridge";
import { KnowledgeCanvas } from "./components/KnowledgeCanvas";"""

if "KnowledgeCanvas" not in content:
    content = content.replace(import_old, import_new)

render_old = """            <Center mt="xs">
              <Text size="xs" c="dimmed">Kolibri AI — Native C & WASM Cognitive Engine</Text>
            </Center>"""

render_new = """            <Center mt="xs">
              <Text size="xs" c="dimmed">Kolibri AI — Native C & WASM Cognitive Engine</Text>
            </Center>
            
            <Paper mt="md" p="md" radius="md" withBorder>
              <Text size="sm" fw={600} mb="xs">Swarm Network Status</Text>
              <KnowledgeCanvas />
            </Paper>"""

if "<KnowledgeCanvas />" not in content:
    content = content.replace(render_old, render_new)

with open("web/src/App.tsx", "w") as f:
    f.write(content)
