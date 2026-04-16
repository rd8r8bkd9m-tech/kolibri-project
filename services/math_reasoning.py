"""
math_reasoning.py — Mathematical Reasoning для Kolibri

Фаза B2: Решение математических задач через формулы
- Символьные вычисления
- Step-by-step решения
- GSM8K-style задачи
"""
from __future__ import annotations

import logging
import re
import time
from dataclasses import dataclass, field
from typing import Optional

log = logging.getLogger("kolibri.math")

# ============================================================================
# Math Problem
# ============================================================================

@dataclass
class MathProblem:
    """Математическая задача."""
    question: str
    answer: float | str = ""
    steps: list[str] = field(default_factory=list)
    category: str = "arithmetic"
    difficulty: int = 1


@dataclass
class MathSolution:
    """Решение математической задачи."""
    problem: str
    answer: float | str
    steps: list[str]
    confidence: float
    duration_ms: float
    method: str


# ============================================================================
# Math Solver
# ============================================================================

class MathSolver:
    """Решатель математических задач."""

    def solve(self, question: str) -> MathSolution | None:
        """Решить математическую задачу."""
        t0 = time.time()

        # Определяем тип задачи
        problem_type = self._classify_problem(question)

        if problem_type == "arithmetic":
            return self._solve_arithmetic(question, t0)
        elif problem_type == "percentage":
            return self._solve_percentage(question, t0)
        elif problem_type == "algebra":
            return self._solve_algebra(question, t0)
        elif problem_type == "geometry":
            return self._solve_geometry(question, t0)
        elif problem_type == "word_problem":
            return self._solve_word_problem(question, t0)

        return None

    def _classify_problem(self, question: str) -> str:
        """Классифицировать тип задачи."""
        q = question.lower()

        if any(word in q for word in ["%", "процент", "percent"]):
            return "percentage"
        elif any(word in q for word in ["x", "уравнен", "найти x", "solve for"]):
            return "algebra"
        elif any(word in q for word in ["площад", "периметр", "объем", "area", "perimeter"]):
            return "geometry"
        elif any(word in q for word in ["сколько", "сколько всего", "сколько осталось"]):
            return "word_problem"
        elif re.search(r'[\d]+\s*[+\-*/×÷]\s*[\d]+', q):
            return "arithmetic"

        return "arithmetic"

    def _solve_arithmetic(self, question: str, t0: float) -> MathSolution:
        """Решить арифметическую задачу."""
        steps = []

        # Извлекаем выражение
        match = re.search(r'([\d\s\+\-\*/\.\(\)]+)', question)
        if not match:
            return MathSolution(
                problem=question,
                answer="Не удалось извлечь выражение",
                steps=["Не удалось распознать математическое выражение"],
                confidence=0.0,
                duration_ms=(time.time() - t0) * 1000,
                method="arithmetic-failed",
            )

        expr = match.group(1).strip()
        steps.append(f"Извлечено выражение: {expr}")

        try:
            # Безопасное вычисление
            result = self._safe_eval(expr)
            steps.append(f"Результат: {result}")
            confidence = 0.95
        except Exception as e:
            result = f"Ошибка: {e}"
            steps.append(f"Ошибка вычисления")
            confidence = 0.1

        return MathSolution(
            problem=question,
            answer=result,
            steps=steps,
            confidence=confidence,
            duration_ms=(time.time() - t0) * 1000,
            method="arithmetic",
        )

    def _solve_percentage(self, question: str, t0: float) -> MathSolution:
        """Решить задачу на проценты."""
        steps = []

        # Паттерн: X% от Y
        match = re.search(r'(\d+(?:\.\d+)?)\s*%\s*от\s*(\d+(?:\.\d+)?)', question, re.IGNORECASE)
        if match:
            percent = float(match.group(1))
            value = float(match.group(2))
            result = (percent / 100) * value
            steps.append(f"Задача: найти {percent}% от {value}")
            steps.append(f"Формула: ({percent} / 100) × {value}")
            steps.append(f"Результат: {result}")
            return MathSolution(
                problem=question,
                answer=result,
                steps=steps,
                confidence=0.95,
                duration_ms=(time.time() - t0) * 1000,
                method="percentage",
            )

        # Паттерн: X составляет Y% от чего?
        match = re.search(r'(\d+(?:\.\d+)?)\s*составляет\s*(\d+(?:\.\d+)?)\s*%', question, re.IGNORECASE)
        if match:
            value = float(match.group(1))
            percent = float(match.group(2))
            result = (value / percent) * 100
            steps.append(f"Задача: {value} составляет {percent}% от X")
            steps.append(f"Формула: X = {value} / ({percent} / 100)")
            steps.append(f"Результат: {result}")
            return MathSolution(
                problem=question,
                answer=result,
                steps=steps,
                confidence=0.95,
                duration_ms=(time.time() - t0) * 1000,
                method="percentage-reverse",
            )

        return MathSolution(
            problem=question,
            answer="Не удалось распознать задачу на проценты",
            steps=["Не удалось распознать паттерн"],
            confidence=0.1,
            duration_ms=(time.time() - t0) * 1000,
            method="percentage-failed",
        )

    def _solve_algebra(self, question: str, t0: float) -> MathSolution:
        """Решить алгебраическую задачу."""
        steps = []

        # Простое уравнение: ax + b = c
        match = re.search(r'(\d*)\s*\*\s*x\s*\+\s*(\d+)\s*=\s*(\d+)', question)
        if match:
            a = int(match.group(1)) if match.group(1) else 1
            b = int(match.group(2))
            c = int(match.group(3))
            x = (c - b) / a
            steps.append(f"Уравнение: {a}x + {b} = {c}")
            steps.append(f"Шаг 1: {a}x = {c} - {b} = {c - b}")
            steps.append(f"Шаг 2: x = {c - b} / {a} = {x}")
            return MathSolution(
                problem=question,
                answer=x,
                steps=steps,
                confidence=0.9,
                duration_ms=(time.time() - t0) * 1000,
                method="algebra-linear",
            )

        return MathSolution(
            problem=question,
            answer="Не удалось распознать уравнение",
            steps=["Не удалось распознать паттерн уравнения"],
            confidence=0.1,
            duration_ms=(time.time() - t0) * 1000,
            method="algebra-failed",
        )

    def _solve_geometry(self, question: str, t0: float) -> MathSolution:
        """Решить геометрическую задачу."""
        steps = []

        # Площадь прямоугольника
        match = re.search(r'площад.*прямоугольник.*(\d+)\s*[xх×]\s*(\d+)', question, re.IGNORECASE)
        if match:
            a = int(match.group(1))
            b = int(match.group(2))
            area = a * b
            steps.append(f"Задача: площадь прямоугольника {a}×{b}")
            steps.append(f"Формула: S = a × b")
            steps.append(f"Результат: S = {a} × {b} = {area}")
            return MathSolution(
                problem=question,
                answer=area,
                steps=steps,
                confidence=0.95,
                duration_ms=(time.time() - t0) * 1000,
                method="geometry-rectangle",
            )

        return MathSolution(
            problem=question,
            answer="Не удалось распознать геометрическую задачу",
            steps=["Не удалось распознать паттерн"],
            confidence=0.1,
            duration_ms=(time.time() - t0) * 1000,
            method="geometry-failed",
        )

    def _solve_word_problem(self, question: str, t0: float) -> MathSolution:
        """Решить текстовую задачу."""
        steps = []

        # Извлекаем числа
        numbers = [float(x) for x in re.findall(r'(\d+(?:\.\d+)?)', question)]
        if len(numbers) >= 2:
            # Определяем операцию
            if any(word in question.lower() for word in ["всего", "вместе", "сумм", "total"]):
                result = sum(numbers)
                steps.append(f"Извлечены числа: {numbers}")
                steps.append(f"Операция: сложение")
                steps.append(f"Результат: {' + '.join(map(str, numbers))} = {result}")
                return MathSolution(
                    problem=question,
                    answer=result,
                    steps=steps,
                    confidence=0.8,
                    duration_ms=(time.time() - t0) * 1000,
                    method="word-problem-addition",
                )
            elif any(word in question.lower() for word in ["остал", "разниц", "difference"]):
                result = numbers[0] - sum(numbers[1:])
                steps.append(f"Извлечены числа: {numbers}")
                steps.append(f"Операция: вычитание")
                steps.append(f"Результат: {numbers[0]} - {' - '.join(map(str, numbers[1:]))} = {result}")
                return MathSolution(
                    problem=question,
                    answer=result,
                    steps=steps,
                    confidence=0.8,
                    duration_ms=(time.time() - t0) * 1000,
                    method="word-problem-subtraction",
                )
            elif any(word in question.lower() for word in ["кажд", "умнож", "times", "each"]):
                result = numbers[0] * numbers[1] if len(numbers) >= 2 else 0
                steps.append(f"Извлечены числа: {numbers[:2]}")
                steps.append(f"Операция: умножение")
                steps.append(f"Результат: {numbers[0]} × {numbers[1]} = {result}")
                return MathSolution(
                    problem=question,
                    answer=result,
                    steps=steps,
                    confidence=0.8,
                    duration_ms=(time.time() - t0) * 1000,
                    method="word-problem-multiplication",
                )

        return MathSolution(
            problem=question,
            answer="Не удалось распознать текстовую задачу",
            steps=["Не удалось извлечь числа или операцию"],
            confidence=0.1,
            duration_ms=(time.time() - t0) * 1000,
            method="word-problem-failed",
        )

    @staticmethod
    def _safe_eval(expr: str) -> float:
        """Безопасное вычисление выражения."""
        # Разрешаем только цифры, операторы и скобки
        if not re.match(r'^[\d\s\+\-\*/\.\(\)]+$', expr):
            raise ValueError(f"Invalid expression: {expr}")
        return eval(expr)


# ============================================================================
# Math Reasoning Pipeline
# ============================================================================

class MathReasoningPipeline:
    """Полный pipeline математического reasoning."""

    def __init__(self) -> None:
        self.solver = MathSolver()

    def solve(self, question: str) -> dict:
        """Решить математическую задачу и вернуть ответ с шагами."""
        solution = self.solver.solve(question)

        if not solution:
            return {
                "response": "Не удалось решить задачу",
                "confidence": 0.0,
                "method": "math-failed",
                "duration_ms": 0,
            }

        # Форматируем ответ
        response_parts = []
        for step in solution.steps:
            response_parts.append(step)

        response = "\n".join(response_parts)

        return {
            "response": response,
            "answer": solution.answer,
            "confidence": solution.confidence,
            "method": solution.method,
            "duration_ms": round(solution.duration_ms, 1),
            "steps": solution.steps,
        }
