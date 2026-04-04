"""
function_calling.py — Function Calling для Kolibri

Фаза B3: Формулы как инструменты
- "Вычисли 2+2" → формула → результат
- API integration через формулы
- Tool selection через intent detection
"""
from __future__ import annotations

import json
import logging
import re
import time
from dataclasses import dataclass, field
from typing import Any, Callable

log = logging.getLogger("kolibri.function_calling")

# ============================================================================
# Tool Definition
# ============================================================================

@dataclass
class ToolParameter:
    """Параметр инструмента."""
    name: str
    type: str  # "string", "number", "boolean"
    description: str
    required: bool = True


@dataclass
class ToolDefinition:
    """Определение инструмента."""
    name: str
    description: str
    parameters: list[ToolParameter]
    handler: Callable


# ============================================================================
# Built-in Tools
# ============================================================================

def _tool_calculator(expression: str) -> dict:
    """Вычислить математическое выражение."""
    try:
        # Безопасное вычисление
        if not re.match(r'^[\d\s\+\-\*/\.\(\)]+$', expression):
            return {"error": "Invalid expression"}
        result = eval(expression)
        return {"result": result, "expression": expression}
    except Exception as e:
        return {"error": str(e)}


def _tool_weather(location: str) -> dict:
    """Получить погоду в локации."""
    # Заглушка — в реальной реализации: API вызов
    return {
        "location": location,
        "temperature": 20,
        "condition": "sunny",
        "note": "Mock data — implement real API",
    }


def _tool_time(location: str = "UTC") -> dict:
    """Получить текущее время."""
    from datetime import datetime
    return {
        "time": datetime.now().isoformat(),
        "timezone": location,
    }


def _tool_search(query: str) -> dict:
    """Поиск информации."""
    return {
        "query": query,
        "results": [],
        "note": "Implement search integration",
    }


# ============================================================================
# Function Calling Pipeline
# ============================================================================

class FunctionCallingPipeline:
    """Pipeline для function calling."""

    def __init__(self) -> None:
        self.tools: dict[str, ToolDefinition] = {}
        self._register_builtin_tools()

    def _register_builtin_tools(self) -> None:
        """Зарегистрировать встроенные инструменты."""
        self.tools["calculator"] = ToolDefinition(
            name="calculator",
            description="Вычислить математическое выражение",
            parameters=[
                ToolParameter("expression", "string", "Математическое выражение, например '2 + 2 * 3'"),
            ],
            handler=_tool_calculator,
        )

        self.tools["weather"] = ToolDefinition(
            name="weather",
            description="Получить погоду в локации",
            parameters=[
                ToolParameter("location", "string", "Название города"),
            ],
            handler=_tool_weather,
        )

        self.tools["time"] = ToolDefinition(
            name="time",
            description="Получить текущее время",
            parameters=[
                ToolParameter("location", "string", "Часовой пояс", required=False),
            ],
            handler=_tool_time,
        )

        self.tools["search"] = ToolDefinition(
            name="search",
            description="Поиск информации",
            parameters=[
                ToolParameter("query", "string", "Поисковый запрос"),
            ],
            handler=_tool_search,
        )

    def detect_tool_call(self, message: str) -> dict | None:
        """Определить нужно ли вызывать инструмент."""
        msg_lower = message.lower()

        # Calculator
        if any(word in msg_lower for word in ["вычисли", "посчитай", "калькул", "сколько будет"]):
            match = re.search(r'([\d\s\+\-\*/\.\(\)]+)', message)
            if match:
                return {
                    "tool": "calculator",
                    "arguments": {"expression": match.group(1).strip()},
                }

        # Weather
        if any(word in msg_lower for word in ["погод", "температур", "дождь", "снег"]):
            match = re.search(r'(?:в|городе|город)\s+([А-Яа-яA-Za-z\s]+)', message)
            location = match.group(1).strip() if match else "Москва"
            return {
                "tool": "weather",
                "arguments": {"location": location},
            }

        # Time
        if any(word in msg_lower for word in ["время", "который час", "сколько время"]):
            match = re.search(r'(?:в|городе|город|часовой\s+пояс)\s+([А-Яа-яA-Za-z\s]+)', message)
            location = match.group(1).strip() if match else "UTC"
            return {
                "tool": "time",
                "arguments": {"location": location},
            }

        # Search
        if any(word in msg_lower for word in ["найди", "поищи", "что такое", "кто такой"]):
            return {
                "tool": "search",
                "arguments": {"query": message},
            }

        return None

    def execute_tool(self, tool_call: dict) -> dict:
        """Выполнить инструмент."""
        tool_name = tool_call.get("tool")
        arguments = tool_call.get("arguments", {})

        if tool_name not in self.tools:
            return {"error": f"Unknown tool: {tool_name}"}

        tool = self.tools[tool_name]

        # Валидация параметров
        for param in tool.parameters:
            if param.required and param.name not in arguments:
                return {"error": f"Missing required parameter: {param.name}"}

        # Выполнение
        try:
            result = tool.handler(**arguments)
            return {
                "tool": tool_name,
                "arguments": arguments,
                "result": result,
                "success": True,
            }
        except Exception as e:
            return {
                "tool": tool_name,
                "arguments": arguments,
                "error": str(e),
                "success": False,
            }

    def format_response(self, tool_result: dict) -> str:
        """Форматировать результат инструмента в ответ."""
        if not tool_result.get("success"):
            return f"Ошибка: {tool_result.get('error', 'Неизвестная ошибка')}"

        tool_name = tool_result.get("tool")
        result = tool_result.get("result", {})

        if tool_name == "calculator":
            return f"Результат: {result.get('result')} ({result.get('expression', '')})"
        elif tool_name == "weather":
            return (
                f"Погода в {result.get('location')}: "
                f"{result.get('temperature')}°C, {result.get('condition')}"
            )
        elif tool_name == "time":
            return f"Текущее время ({result.get('timezone')}): {result.get('time')}"
        elif tool_name == "search":
            return f"Поиск: '{result.get('query')}' — результатов: {len(result.get('results', []))}"

        return json.dumps(result, ensure_ascii=False, indent=2)
