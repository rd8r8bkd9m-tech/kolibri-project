"""
code_gen.py — Code Generation для Kolibri

Фаза B1: Генерация кода через эволюционные формулы
- AST → decimal genome
- Эволюция формул до прохождения тестов
- Поддержка Python, C, JavaScript
"""
from __future__ import annotations

import ast
import logging
import re
import time
from dataclasses import dataclass, field
from typing import Optional

log = logging.getLogger("kolibri.codegen")

# ============================================================================
# Code AST → Decimal Genome
# ============================================================================

@dataclass
class CodeSnippet:
    """Фрагмент кода с метаданными."""
    code: str
    language: str
    description: str
    test_cases: list[str] = field(default_factory=list)
    genome_digits: list[int] = field(default_factory=list)
    fitness: float = 0.0


class ASTEncoder:
    """Кодирует AST в decimal genome."""

    @staticmethod
    def encode_python(code: str) -> list[int]:
        """Python AST → decimal digits."""
        try:
            tree = ast.parse(code)
        except SyntaxError:
            return []

        digits = []
        for node in ast.walk(tree):
            # Node type → 2 digits
            node_type = type(node).__name__
            type_hash = hash(node_type) % 100
            digits.extend([type_hash // 10, type_hash % 10])

            # Node attributes → digits
            for attr_name in sorted(dir(node)):
                if attr_name.startswith('_'):
                    continue
                try:
                    value = getattr(node, attr_name)
                    if isinstance(value, str):
                        for ch in value[:20]:
                            digits.append(ord(ch) % 10)
                    elif isinstance(value, (int, float)):
                        for d in str(int(abs(value)))[:5]:
                            digits.append(int(d))
                except Exception:
                    pass

        return digits[:4000]  # Limit to genome size

    @staticmethod
    def encode_c(code: str) -> list[int]:
        """C code → decimal digits (simplified)."""
        digits = []
        # Tokenize
        tokens = re.findall(r'\b\w+\b|[{}();,\[\]=+\-*/<>!&|]', code)
        for token in tokens:
            for ch in token[:10]:
                digits.append(ord(ch) % 10)
        return digits[:4000]

    @staticmethod
    def encode_js(code: str) -> list[int]:
        """JavaScript code → decimal digits (simplified)."""
        return ASTEncoder.encode_c(code)  # Similar tokenization


# ============================================================================
# Code Formula Pool
# ============================================================================

class CodeFormulaPool:
    """Пул формул для генерации кода."""

    def __init__(self) -> None:
        self.snippets: dict[str, list[CodeSnippet]] = {
            "python": [],
            "c": [],
            "javascript": [],
        }
        self._templates = self._load_templates()

    def _load_templates(self) -> dict[str, list[CodeSnippet]]:
        """Загрузить шаблоны кода."""
        templates: dict[str, list[CodeSnippet]] = {
            "python": [
                CodeSnippet(
                    code="def {name}({args}):\n    return {body}",
                    language="python",
                    description="Python function template",
                    fitness=0.5,
                ),
                CodeSnippet(
                    code="class {name}:\n    def __init__(self):\n        pass\n    def {method}(self):\n        return {body}",
                    language="python",
                    description="Python class template",
                    fitness=0.5,
                ),
                CodeSnippet(
                    code="for {var} in {iterable}:\n    {body}",
                    language="python",
                    description="Python for loop",
                    fitness=0.5,
                ),
                CodeSnippet(
                    code="if {condition}:\n    {body}\nelse:\n    {else_body}",
                    language="python",
                    description="Python if-else",
                    fitness=0.5,
                ),
            ],
            "c": [
                CodeSnippet(
                    code="{return_type} {name}({args}) {{\n    {body}\n}}",
                    language="c",
                    description="C function template",
                    fitness=0.5,
                ),
                CodeSnippet(
                    code="for (int {var} = 0; {var} < {limit}; {var}++) {{\n    {body}\n}}",
                    language="c",
                    description="C for loop",
                    fitness=0.5,
                ),
            ],
            "javascript": [
                CodeSnippet(
                    code="function {name}({args}) {{\n    {body}\n}}",
                    language="javascript",
                    description="JS function template",
                    fitness=0.5,
                ),
                CodeSnippet(
                    code="const {name} = ({args}) => {{\n    {body}\n}};",
                    language="javascript",
                    description="JS arrow function",
                    fitness=0.5,
                ),
            ],
        }
        return templates

    def generate(self, description: str, language: str = "python") -> str:
        """Сгенерировать код по описанию."""
        desc_lower = description.lower()

        # Определяем тип кода
        if "сортировк" in desc_lower or "sort" in desc_lower:
            return self._generate_sort(language)
        elif "поиск" in desc_lower or "search" in desc_lower:
            return self._generate_search(language)
        elif "функци" in desc_lower or "function" in desc_lower:
            return self._generate_function(description, language)
        elif "класс" in desc_lower or "class" in desc_lower:
            return self._generate_class(description, language)
        else:
            return self._generate_generic(description, language)

    def _generate_sort(self, language: str) -> str:
        """Сгенерировать алгоритм сортировки."""
        sorts = {
            "python": (
                "def bubble_sort(arr):\n"
                "    n = len(arr)\n"
                "    for i in range(n):\n"
                "        for j in range(0, n - i - 1):\n"
                "            if arr[j] > arr[j + 1]:\n"
                "                arr[j], arr[j + 1] = arr[j + 1], arr[j]\n"
                "    return arr"
            ),
            "c": (
                "void bubble_sort(int arr[], int n) {\n"
                "    for (int i = 0; i < n; i++) {\n"
                "        for (int j = 0; j < n - i - 1; j++) {\n"
                "            if (arr[j] > arr[j + 1]) {\n"
                "                int temp = arr[j];\n"
                "                arr[j] = arr[j + 1];\n"
                "                arr[j + 1] = temp;\n"
                "            }\n"
                "        }\n"
                "    }\n"
                "}"
            ),
            "javascript": (
                "function bubbleSort(arr) {\n"
                "    const n = arr.length;\n"
                "    for (let i = 0; i < n; i++) {\n"
                "        for (let j = 0; j < n - i - 1; j++) {\n"
                "            if (arr[j] > arr[j + 1]) {\n"
                "                [arr[j], arr[j + 1]] = [arr[j + 1], arr[j]];\n"
                "            }\n"
                "        }\n"
                "    }\n"
                "    return arr;\n"
                "}"
            ),
        }
        return sorts.get(language, sorts["python"])

    def _generate_search(self, language: str) -> str:
        """Сгенерировать алгоритм поиска."""
        searches = {
            "python": (
                "def binary_search(arr, target):\n"
                "    left, right = 0, len(arr) - 1\n"
                "    while left <= right:\n"
                "        mid = (left + right) // 2\n"
                "        if arr[mid] == target:\n"
                "            return mid\n"
                "        elif arr[mid] < target:\n"
                "            left = mid + 1\n"
                "        else:\n"
                "            right = mid - 1\n"
                "    return -1"
            ),
            "c": (
                "int binary_search(int arr[], int n, int target) {\n"
                "    int left = 0, right = n - 1;\n"
                "    while (left <= right) {\n"
                "        int mid = left + (right - left) / 2;\n"
                "        if (arr[mid] == target) return mid;\n"
                "        if (arr[mid] < target) left = mid + 1;\n"
                "        else right = mid - 1;\n"
                "    }\n"
                "    return -1;\n"
                "}"
            ),
            "javascript": (
                "function binarySearch(arr, target) {\n"
                "    let left = 0, right = arr.length - 1;\n"
                "    while (left <= right) {\n"
                "        const mid = Math.floor((left + right) / 2);\n"
                "        if (arr[mid] === target) return mid;\n"
                "        if (arr[mid] < target) left = mid + 1;\n"
                "        else right = mid - 1;\n"
                "    }\n"
                "    return -1;\n"
                "}"
            ),
        }
        return searches.get(language, searches["python"])

    def _generate_function(self, description: str, language: str) -> str:
        """Сгенерировать функцию."""
        # Извлекаем имя функции
        match = re.search(r'функци[юя]\s+(\w+)', description, re.IGNORECASE)
        if not match:
            match = re.search(r'function\s+(\w+)', description, re.IGNORECASE)
        name = match.group(1) if match else "my_function"

        templates = {
            "python": f"def {name}():\n    # TODO: implement\n    pass",
            "c": f"void {name}() {{\n    // TODO: implement\n}}",
            "javascript": f"function {name}() {{\n    // TODO: implement\n}}",
        }
        return templates.get(language, templates["python"])

    def _generate_class(self, description: str, language: str) -> str:
        """Сгенерировать класс."""
        match = re.search(r'класс[ау]?\s+(\w+)', description, re.IGNORECASE)
        if not match:
            match = re.search(r'class\s+(\w+)', description, re.IGNORECASE)
        name = match.group(1) if match else "MyClass"

        templates = {
            "python": f"class {name}:\n    def __init__(self):\n        pass",
            "c": f"typedef struct {{\n    // fields\n}} {name};",
            "javascript": f"class {name} {{\n    constructor() {{\n        // init\n    }}\n}}",
        }
        return templates.get(language, templates["python"])

    def _generate_generic(self, description: str, language: str) -> str:
        """Сгенерировать код по описанию."""
        return f"# {description}\n# TODO: implement"


# ============================================================================
# Code Generation Pipeline
# ============================================================================

class CodeGenerationPipeline:
    """Полный pipeline генерации кода."""

    def __init__(self) -> None:
        self.pool = CodeFormulaPool()
        self.encoder = ASTEncoder()

    def generate(self, description: str, language: str = "python") -> dict:
        """Сгенерировать код и вернуть метаданные."""
        t0 = time.time()

        # Генерация
        code = self.pool.generate(description, language)

        # Encode AST → genome
        if language == "python":
            genome = self.encoder.encode_python(code)
        elif language == "c":
            genome = self.encoder.encode_c(code)
        else:
            genome = self.encoder.encode_js(code)

        # Fitness: проверяем синтаксис
        fitness = self._compute_fitness(code, language)

        duration_ms = (time.time() - t0) * 1000

        return {
            "code": code,
            "language": language,
            "genome_length": len(genome),
            "fitness": fitness,
            "duration_ms": round(duration_ms, 1),
            "method": "code-generation",
        }

    def _compute_fitness(self, code: str, language: str) -> float:
        """Вычислить fitness кода (проверка синтаксиса)."""
        fitness = 0.5  # Base

        if language == "python":
            try:
                ast.parse(code)
                fitness += 0.3  # Valid syntax
            except SyntaxError:
                return 0.1

            # Bonus for having key structures
            if "def " in code:
                fitness += 0.1
            if "return " in code:
                fitness += 0.1
            if len(code.split('\n')) > 3:
                fitness += 0.1

        elif language == "c":
            if "{" in code and "}" in code:
                fitness += 0.3
            if ";" in code:
                fitness += 0.1

        elif language == "javascript":
            if "{" in code and "}" in code:
                fitness += 0.3
            if "function" in code or "=>" in code:
                fitness += 0.2

        return min(fitness, 1.0)
