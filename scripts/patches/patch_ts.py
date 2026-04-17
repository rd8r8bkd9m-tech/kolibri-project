import re

with open("web/src/components/KnowledgeCanvas.tsx", "r") as f:
    content = f.read()

content = content.replace("navigator.gpu", "(navigator as any).gpu")
content = content.replace("const context = canvas.getContext(\"webgpu\");", "const context = canvas.getContext(\"webgpu\") as any;")
content = content.replace("GPUBufferUsage", "(window as any).GPUBufferUsage")

with open("web/src/components/KnowledgeCanvas.tsx", "w") as f:
    f.write(content)
