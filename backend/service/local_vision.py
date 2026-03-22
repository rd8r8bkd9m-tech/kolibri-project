from __future__ import annotations

import io
import math
import re
import shutil
import subprocess
import tempfile
from statistics import pstdev
from typing import Optional


def _color_name(rgb: tuple[int, int, int]) -> str:
    r, g, b = rgb
    mx = max(rgb)
    mn = min(rgb)
    if mx < 35:
        return "почти чёрный"
    if mn > 220:
        return "почти белый"
    if mx - mn < 18:
        if mx < 95:
            return "тёмно-серый"
        if mx > 180:
            return "светло-серый"
        return "серый"
    if r >= g and r >= b:
        if g > 150 and b < 120:
            return "жёлто-оранжевый"
        if b > 110:
            return "розово-фиолетовый"
        return "красный"
    if g >= r and g >= b:
        if r > 150:
            return "жёлто-зелёный"
        if b > 130:
            return "бирюзовый"
        return "зелёный"
    if r > 150 and g > 150:
        return "светло-голубой"
    return "синий"


def _clean_text(value: str, limit: int = 220) -> str:
    normalized = re.sub(r"\s+", " ", value or "").strip()
    if len(normalized) <= limit:
        return normalized
    return normalized[: limit - 1].rstrip() + "…"


def _extract_text_with_tesseract(binary: bytes) -> str:
    tesseract_path = shutil.which("tesseract")
    if not tesseract_path:
        return ""

    with tempfile.NamedTemporaryFile(suffix=".png", delete=False) as temp_file:
        temp_file.write(binary)
        temp_path = temp_file.name

    try:
        process = subprocess.run(
            [tesseract_path, temp_path, "stdout", "-l", "rus+eng", "--psm", "6"],
            capture_output=True,
            text=True,
            timeout=20,
            check=False,
        )
        if process.returncode != 0:
            return ""
        return _clean_text(process.stdout, limit=320)
    except Exception:
        return ""
    finally:
        try:
            import os

            os.unlink(temp_path)
        except OSError:
            pass


def _metadata_only_response(
    *,
    file_name: str,
    mime_type: str,
    binary: bytes,
    width: Optional[int],
    height: Optional[int],
    reason: str | None = None,
) -> str:
    details = [
        f"Получено изображение {file_name or 'без имени'}",
        f"тип: {mime_type}",
        f"размер: {max(1, len(binary) // 1024)} КБ",
    ]
    if width and height:
        details.append(f"разрешение: {width}x{height}")

    response = "Изображение получено. " + "; ".join(details) + "."
    if reason:
        response += f" Локальный разбор ограничен: {reason}."
    else:
        response += " Сейчас доступен базовый локальный разбор без внешних моделей."
    return response


def analyze_local_image(
    *,
    binary: bytes,
    file_name: str,
    mime_type: str,
    prompt: str,
    width: Optional[int],
    height: Optional[int],
) -> str:
    try:
        from PIL import Image, ImageFilter  # type: ignore
    except Exception:
        return _metadata_only_response(
            file_name=file_name,
            mime_type=mime_type,
            binary=binary,
            width=width,
            height=height,
            reason="библиотека Pillow не установлена",
        )

    try:
        with Image.open(io.BytesIO(binary)) as opened:
            image = opened.convert("RGB")
            src_width, src_height = image.size
            width = width or src_width
            height = height or src_height

            thumb = image.copy()
            thumb.thumbnail((160, 160))
            rgb_pixels = list(thumb.getdata())
            if not rgb_pixels:
                return _metadata_only_response(
                    file_name=file_name,
                    mime_type=mime_type,
                    binary=binary,
                    width=width,
                    height=height,
                    reason="не удалось извлечь пиксели",
                )

            brightness_values = []
            saturation_values = []
            gray_like = 0
            bright = 0
            dark = 0
            for r, g, b in rgb_pixels:
                brightness = 0.299 * r + 0.587 * g + 0.114 * b
                brightness_values.append(brightness)
                mx = max(r, g, b)
                mn = min(r, g, b)
                saturation_values.append(0.0 if mx == 0 else (mx - mn) / mx)
                if max(abs(r - g), abs(g - b), abs(r - b)) < 14:
                    gray_like += 1
                if brightness > 215:
                    bright += 1
                if brightness < 40:
                    dark += 1

            avg_brightness = sum(brightness_values) / len(brightness_values)
            avg_saturation = sum(saturation_values) / len(saturation_values)
            contrast = pstdev(brightness_values) if len(brightness_values) > 1 else 0.0
            grayscale_ratio = gray_like / len(rgb_pixels)
            bright_ratio = bright / len(rgb_pixels)
            dark_ratio = dark / len(rgb_pixels)

            edges = thumb.convert("L").filter(ImageFilter.FIND_EDGES)
            edge_values = list(edges.getdata())
            edge_density = 0.0
            if edge_values:
                edge_density = sum(1 for value in edge_values if value > 42) / len(edge_values)

            palette_image = thumb.quantize(colors=4)
            raw_palette = palette_image.getpalette() or []
            palette_colors: list[str] = []
            color_counts = sorted((palette_image.getcolors() or []), reverse=True)
            for _, idx in color_counts[:3]:
                base = idx * 3
                if base + 2 >= len(raw_palette):
                    continue
                color = (
                    int(raw_palette[base]),
                    int(raw_palette[base + 1]),
                    int(raw_palette[base + 2]),
                )
                color_name = _color_name(color)
                if color_name not in palette_colors:
                    palette_colors.append(color_name)
            if not palette_colors:
                palette_colors = ["неопределённые"]

            ocr_text = _extract_text_with_tesseract(binary)
    except Exception as exc:
        return _metadata_only_response(
            file_name=file_name,
            mime_type=mime_type,
            binary=binary,
            width=width,
            height=height,
            reason=f"ошибка локального разбора: {type(exc).__name__}",
        )

    aspect_ratio = (width or 1) / max(1, height or 1)
    orientation = "горизонтальное" if aspect_ratio > 1.15 else "вертикальное" if aspect_ratio < 0.87 else "почти квадратное"

    if ocr_text and (bright_ratio > 0.42 or grayscale_ratio > 0.45):
        scene_kind = "скриншот, документ или интерфейс с текстом"
    elif bright_ratio > 0.58 and edge_density > 0.08:
        scene_kind = "документ, схема или светлый интерфейс"
    elif avg_saturation > 0.28 and contrast > 36:
        scene_kind = "фотография или насыщенная иллюстрация"
    elif grayscale_ratio > 0.55 and contrast < 30:
        scene_kind = "схема, иконка или минималистичный интерфейс"
    else:
        scene_kind = "обычное изображение"

    details: list[str] = [
        f"Я вижу {scene_kind}. Формат кадра {orientation}, разрешение {width}x{height}.",
        f"По тону изображение {'светлое' if avg_brightness > 170 else 'тёмное' if avg_brightness < 85 else 'сбалансированное'}, контраст {'высокий' if contrast > 55 else 'умеренный' if contrast > 28 else 'мягкий'}.",
        f"Основные цвета: {', '.join(palette_colors)}.",
    ]

    if dark_ratio > 0.45:
        details.append("В кадре много тёмных областей.")
    if edge_density > 0.12:
        details.append("Границы и контуры выражены довольно чётко.")

    prompt_lc = (prompt or "").lower()
    wants_text = any(token in prompt_lc for token in ("текст", "что написано", "прочитай", "ocr", "надпись"))

    if ocr_text:
        details.append(f"На изображении распознан текст: «{_clean_text(ocr_text)}».")
    elif wants_text:
        details.append("Читаемый текст локально не распознан.")

    if "таблиц" in prompt_lc or "график" in prompt_lc:
        details.append("Если нужен точный разбор таблицы или графика, лучше загрузить более чёткое изображение без сжатия.")
    elif not ocr_text and scene_kind.startswith("фотография"):
        details.append("Локально я могу уверенно описывать общие визуальные признаки, но без внешней модели не идентифицирую редкие объекты по названию.")

    return " ".join(details)
