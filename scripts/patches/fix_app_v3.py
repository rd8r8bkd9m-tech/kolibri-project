import re

with open("web/src/App.tsx", "r") as f:
    content = f.read()

# 1. Fix duration_ms.toFixed(0)
content = content.replace("{msg.meta.duration_ms.toFixed(0)}ms", "{(msg.meta.duration_ms || 0).toFixed(0)}ms")

# 2. Fix health.uptime_ms
content = content.replace("{(health.uptime_ms / 1000).toFixed(1)}s", "{((health.uptime_ms || 0) / 1000).toFixed(1)}s")

# 3. Fix health.avg_response_ms
content = content.replace("{health.avg_response_ms.toFixed(1)}ms", "{(health.avg_response_ms || 0).toFixed(1)}ms")

# 4. Fix duration_ms initializations
content = content.replace("duration_ms: result.duration_ms,", "duration_ms: result.duration_ms || 0,")
content = content.replace("duration_ms: response.duration_ms,", "duration_ms: response.duration_ms || 0,")

with open("web/src/App.tsx", "w") as f:
    f.write(content)

