#!/usr/bin/env python3
"""Kolibri LLM Teacher.

Этот скрипт умеет:
- обращаться к HTTP-совместимому LLM (например Ollama);
- запускать локальную PyTorch/Transformers модель без внешнего сервера.

Полученный ответ автоматически отправляется в Kolibri через teach + положительный
feedback, чтобы база знаний пополнялась сама.
"""

from __future__ import annotations

import argparse
import json
import sys
import urllib.parse
import urllib.request
from typing import Any, Dict


_TORCH_CACHE: Dict[str, Dict[str, Any]] = {}


class TorchModelNameError(ValueError):
    """Ошибка имени модели для backend=torch."""


def _validate_torch_model_name(model_name: str) -> None:
    """Гарантирует, что идентификатор подходит для Hugging Face/локальной папки."""

    if ":" in model_name and "/" not in model_name and not model_name.startswith("./") and not model_name.startswith("../"):
        raise TorchModelNameError(
            "Имя модели содержит ':' (например, 'gemma:2b'). Такие теги относятся к Ollama. "
            "Используйте --backend http для Ollama или укажите репозиторий Hugging Face вроде "
            "'google/gemma-2-2b-it'."
        )


def call_llm_http(url: str, model: str, prompt: str, temperature: float, system_prompt: str) -> str:
    payload: Dict[str, Any] = {
        "model": model,
        "prompt": prompt,
        "stream": False,
        "options": {"temperature": temperature},
    }
    if system_prompt:
        payload["system"] = system_prompt
    data = json.dumps(payload).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        body = response.read().decode("utf-8")
    parsed = json.loads(body)
    if isinstance(parsed, dict):
        if isinstance(parsed.get("response"), str):
            return parsed["response"].strip()
        if parsed.get("choices"):
            first = parsed["choices"][0]
            if isinstance(first, dict) and "text" in first:
                return str(first["text"]).strip()
    raise RuntimeError(f"LLM response did not contain text: {body[:200]}")


def call_llm_torch(model_name: str,
                   prompt: str,
                   temperature: float,
                   max_new_tokens: int,
                   device: str,
                   system_prompt: str) -> str:
    try:
        import torch  # type: ignore
        from transformers import AutoModelForCausalLM, AutoTokenizer  # type: ignore
    except Exception as exc:  # noqa: BLE001
        raise RuntimeError("Для режима torch нужны пакеты torch и transformers") from exc

    _validate_torch_model_name(model_name)

    state = _TORCH_CACHE.get(model_name)
    if state is None:
        tokenizer = AutoTokenizer.from_pretrained(model_name)
        if tokenizer.pad_token is None and tokenizer.eos_token is not None:
            tokenizer.pad_token = tokenizer.eos_token
        model = AutoModelForCausalLM.from_pretrained(model_name)
        if device:
            model = model.to(device)
        model.eval()
        state = {"tokenizer": tokenizer, "model": model, "device": device}
        _TORCH_CACHE[model_name] = state

    tokenizer = state["tokenizer"]
    model = state["model"]
    full_prompt = f"{system_prompt}\n\n{prompt}" if system_prompt else prompt
    encoded = tokenizer(full_prompt, return_tensors="pt")
    input_ids = encoded["input_ids"]
    attention_mask = encoded.get("attention_mask")
    if state["device"]:
        input_ids = input_ids.to(state["device"])
        if attention_mask is not None:
            attention_mask = attention_mask.to(state["device"])

    generate_kwargs = {
        "max_new_tokens": max_new_tokens,
        "do_sample": temperature > 0,
        "temperature": max(temperature, 1e-5),
        "pad_token_id": tokenizer.eos_token_id,
    }
    if attention_mask is not None:
        generate_kwargs["attention_mask"] = attention_mask
    with torch.no_grad():  # type: ignore[attr-defined]
        output = model.generate(input_ids, **generate_kwargs)

    generated = output[0][input_ids.shape[1]:]
    text = tokenizer.decode(generated, skip_special_tokens=True)
    return text.strip()


def send_get(url: str, params: Dict[str, str]) -> None:
    query = urllib.parse.urlencode(params)
    full_url = f"{url}?{query}"
    request = urllib.request.Request(full_url, method="GET")
    with urllib.request.urlopen(request, timeout=15):
        pass


def teach_answer(base_url: str, question: str, answer: str) -> None:
    teach_url = urllib.parse.urljoin(base_url, "/api/knowledge/teach")
    send_get(teach_url, {"q": question, "a": answer})
    feedback_url = urllib.parse.urljoin(base_url, "/api/knowledge/feedback")
    send_get(feedback_url, {"rating": "good", "q": question, "a": answer})


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description="Kolibri LLM auto-teacher")
    parser.add_argument("question", help="Вопрос пользователя")
    parser.add_argument(
        "--backend",
        choices=("http", "torch"),
        default="http",
        help="HTTP — запрос к внешнему API, torch — локальная модель",
    )
    parser.add_argument(
        "--llm-url",
        default="http://127.0.0.1:11434/api/generate",
        help="Endpoint LLM (backend=http)",
    )
    parser.add_argument(
        "--llm-model",
        default="kolibri-teacher",
        help="Имя модели (API или HuggingFace repo для torch)",
    )
    parser.add_argument(
        "--temperature",
        type=float,
        default=0.4,
        help="Температура выборки",
    )
    parser.add_argument(
        "--max-new-tokens",
        type=int,
        default=128,
        help="Максимум генерируемых токенов (torch)",
    )
    parser.add_argument(
        "--device",
        default="",
        help="Устройство torch (cpu, cuda, cuda:0, mps ...)",
    )
    parser.add_argument(
        "--system-prompt",
        default="Отвечай только на русском языке, поддерживай дружелюбный и профессиональный тон.",
        help="Системная инструкция для LLM",
    )
    parser.add_argument(
        "--kolibri-url",
        default="http://127.0.0.1:8000",
        help="Базовый URL Kolibri knowledge service",
    )
    args = parser.parse_args(argv)

    question = args.question.strip()
    if not question:
        print("Empty question is not allowed", file=sys.stderr)
        return 1

    print(f"[kolibri-llm-teacher] Asking LLM ({args.backend}:{args.llm_model})...", file=sys.stderr)
    try:
        if args.backend == "http":
            answer = call_llm_http(args.llm_url, args.llm_model, question, args.temperature, args.system_prompt)
        else:
            answer = call_llm_torch(
                args.llm_model,
                question,
                args.temperature,
                args.max_new_tokens,
                args.device,
                args.system_prompt,
            )
    except TorchModelNameError as exc:
        print(f"[kolibri-llm-teacher] {exc}", file=sys.stderr)
        return 1
    except Exception as exc:  # noqa: BLE001
        print(f"[kolibri-llm-teacher] LLM request failed: {exc}", file=sys.stderr)
        return 1

    if not answer:
        print("[kolibri-llm-teacher] LLM returned empty answer", file=sys.stderr)
        return 1

    print(f"[kolibri-llm-teacher] Teaching Kolibri: {answer}", file=sys.stderr)
    try:
        teach_answer(args.kolibri_url, question, answer)
    except Exception as exc:  # noqa: BLE001
        print(f"[kolibri-llm-teacher] Failed to send teach/feedback: {exc}", file=sys.stderr)
        return 1

    print("[kolibri-llm-teacher] Teach + feedback completed", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
