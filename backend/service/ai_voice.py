"""
ai_voice.py — серверные голосовые эндпоинты для Kolibri.

Поток:
1) Клиент отправляет аудио (base64) на /api/v1/ai/voice/chat-turn
2) Backend делает STT через OpenAI Audio API
3) Полученный текст отправляется в основной AI-движок Kolibri
4) Ответ озвучивается через OpenAI Audio API (TTS)
5) Клиент получает и текст, и готовое аудио ответа
"""
from __future__ import annotations

import asyncio
import base64
import io
import os
import re
import time
import wave
from dataclasses import dataclass
from typing import Optional

import httpx
from fastapi import APIRouter, HTTPException, status
from pydantic import BaseModel, Field

from .ai_engine import get_engine

router = APIRouter(prefix="/api/v1/ai/voice", tags=["ai-voice"])

_MAX_AUDIO_BYTES = 15 * 1024 * 1024
_MIME_TO_EXT = {
    "audio/webm": "webm",
    "audio/webm;codecs=opus": "webm",
    "audio/ogg": "ogg",
    "audio/ogg;codecs=opus": "ogg",
    "audio/wav": "wav",
    "audio/x-wav": "wav",
    "audio/mpeg": "mp3",
    "audio/mp3": "mp3",
    "audio/mp4": "m4a",
    "audio/aac": "aac",
}
_AUDIO_MIME_BY_FORMAT = {
    "mp3": "audio/mpeg",
    "wav": "audio/wav",
    "opus": "audio/ogg",
    "ogg": "audio/ogg",
    "aac": "audio/aac",
    "flac": "audio/flac",
}
_SUPPORTED_VOICE_PROVIDERS = {"openai", "gemini"}
_OPENAI_TO_GEMINI_VOICE = {
    "alloy": "Kore",
    "ash": "Kore",
    "ballad": "Kore",
    "coral": "Kore",
    "echo": "Kore",
    "sage": "Kore",
    "shimmer": "Kore",
    "verse": "Kore",
}
_VOICE_RUNTIME_DISABLED_REASON: Optional[str] = None
_VOICE_RUNTIME_PROBED = False


def _mark_voice_runtime_disabled(reason: str) -> None:
    global _VOICE_RUNTIME_DISABLED_REASON
    if not _VOICE_RUNTIME_DISABLED_REASON:
        _VOICE_RUNTIME_DISABLED_REASON = reason.strip() or "Voice runtime disabled"


def _voice_runtime_disabled_reason() -> Optional[str]:
    return _VOICE_RUNTIME_DISABLED_REASON


async def _probe_gemini_runtime(cfg: "VoiceConfig") -> None:
    global _VOICE_RUNTIME_PROBED
    if _VOICE_RUNTIME_PROBED:
        return
    _VOICE_RUNTIME_PROBED = True
    if cfg.provider != "gemini" or not cfg.api_key:
        return
    payload = {
        "contents": [{"parts": [{"text": "Ping"}]}],
        "generationConfig": {"temperature": 0},
    }
    try:
        async with httpx.AsyncClient(timeout=min(cfg.timeout_sec, 6.0)) as client:
            response = await client.post(
                f"{cfg.gemini_base_url}/models/{cfg.gemini_transcribe_model}:generateContent",
                params={"key": cfg.api_key},
                json=payload,
            )
    except Exception:
        return
    if response.status_code < 400:
        return
    err = _extract_upstream_error(response)
    if "location is not supported" in err.lower():
        _mark_voice_runtime_disabled("Gemini API geo restriction for server location")


def _read_secret_file(path: str) -> Optional[str]:
    if not path:
        return None
    try:
        with open(path, "r", encoding="utf-8") as secret_file:
            value = secret_file.read().strip()
    except OSError:
        return None
    return value or None


@dataclass(frozen=True)
class VoiceConfig:
    provider: str
    api_key: Optional[str]
    openai_base_url: str
    gemini_base_url: str
    openai_transcribe_model: str
    openai_speech_model: str
    gemini_transcribe_model: str
    gemini_speech_model: str
    default_voice: str
    timeout_sec: float

    @property
    def transcribe_model(self) -> str:
        if self.provider == "gemini":
            return self.gemini_transcribe_model
        return self.openai_transcribe_model

    @property
    def speech_model(self) -> str:
        if self.provider == "gemini":
            return self.gemini_speech_model
        return self.openai_speech_model

    @classmethod
    def from_env(cls) -> "VoiceConfig":
        provider_raw = os.getenv("KOLIBRI_VOICE_PROVIDER", "").strip().lower()
        api_key = (
            os.getenv("KOLIBRI_VOICE_API_KEY")
            or os.getenv("OPENAI_API_KEY")
            or os.getenv("KOLIBRI_LLM_API_KEY")
        )
        if not api_key:
            key_file = os.getenv("KOLIBRI_VOICE_API_KEY_FILE", "").strip()
            if not key_file:
                project_root = os.getenv("KOLIBRI_PROJECT_ROOT", "").strip()
                if project_root:
                    key_file = os.path.join(project_root, ".run", "voice_api_key")
            api_key = _read_secret_file(key_file)
        if provider_raw in _SUPPORTED_VOICE_PROVIDERS:
            provider = provider_raw
        elif api_key and api_key.startswith("AIza"):
            provider = "gemini"
        else:
            provider = "openai"

        openai_base_url = os.getenv("KOLIBRI_VOICE_BASE_URL", "https://api.openai.com/v1").strip().rstrip("/")
        gemini_base_url = os.getenv("KOLIBRI_GEMINI_BASE_URL", "https://generativelanguage.googleapis.com/v1beta").strip().rstrip("/")
        openai_transcribe_model = os.getenv("KOLIBRI_VOICE_TRANSCRIBE_MODEL", "gpt-4o-mini-transcribe").strip()
        openai_speech_model = os.getenv("KOLIBRI_VOICE_TTS_MODEL", "gpt-4o-mini-tts").strip()
        gemini_transcribe_model = os.getenv("KOLIBRI_GEMINI_TRANSCRIBE_MODEL", "gemini-2.5-flash").strip()
        gemini_speech_model = os.getenv("KOLIBRI_GEMINI_TTS_MODEL", "gemini-2.5-flash-preview-tts").strip()
        default_voice = os.getenv("KOLIBRI_VOICE_DEFAULT", "Kore" if provider == "gemini" else "alloy").strip()
        timeout_raw = os.getenv("KOLIBRI_VOICE_TIMEOUT", "45").strip()
        try:
            timeout_sec = float(timeout_raw)
        except ValueError:
            timeout_sec = 45.0
        return cls(
            provider=provider,
            api_key=api_key,
            openai_base_url=openai_base_url,
            gemini_base_url=gemini_base_url,
            openai_transcribe_model=openai_transcribe_model,
            openai_speech_model=openai_speech_model,
            gemini_transcribe_model=gemini_transcribe_model,
            gemini_speech_model=gemini_speech_model,
            default_voice=default_voice,
            timeout_sec=max(5.0, timeout_sec),
        )


class VoiceHealthResponse(BaseModel):
    enabled: bool
    provider: str
    transcribe_model: str
    speech_model: str
    default_voice: str
    detail: str


class VoiceTranscribeRequest(BaseModel):
    audio_base64: str = Field(min_length=16, description="Base64-строка аудио (можно data URL)")
    mime_type: str = Field(default="audio/webm", description="MIME аудио")
    language: Optional[str] = Field(default=None, description="Язык распознавания (например ru)")
    prompt: Optional[str] = Field(default=None, description="Контекстная подсказка для распознавания")


class VoiceTranscribeResponse(BaseModel):
    transcript: str
    language: Optional[str] = None
    duration_ms: float


class VoiceSpeechRequest(BaseModel):
    text: str = Field(min_length=1, max_length=6000)
    voice: Optional[str] = Field(default=None, description="Голос, например alloy")
    audio_format: str = Field(default="mp3", description="mp3|wav|opus|ogg|aac|flac")
    speed: float = Field(default=1.0, ge=0.25, le=4.0)


class VoiceSpeechResponse(BaseModel):
    audio_base64: str
    audio_mime: str
    voice: str
    duration_ms: float


class VoiceChatTurnRequest(BaseModel):
    audio_base64: str = Field(min_length=16)
    mime_type: str = Field(default="audio/webm")
    conversation_id: Optional[str] = Field(default=None)
    temperature: float = Field(default=0.65, ge=0.0, le=2.0)
    language: Optional[str] = Field(default=None)
    voice: Optional[str] = Field(default=None)
    audio_format: str = Field(default="mp3")
    speed: float = Field(default=1.0, ge=0.25, le=4.0)
    speak: bool = Field(default=True)


class VoiceChatTurnResponse(BaseModel):
    transcript: str
    response: str
    conversation_id: str
    confidence: float
    duration_ms: float
    audio_base64: Optional[str] = None
    audio_mime: Optional[str] = None
    voice: Optional[str] = None
    method: str = "voice-chat"


def _ensure_voice_enabled(cfg: VoiceConfig) -> None:
    disabled_reason = _voice_runtime_disabled_reason()
    if disabled_reason:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail=f"Voice temporarily disabled: {disabled_reason}",
        )
    if not cfg.api_key:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail="Voice API key is not configured. Set OPENAI_API_KEY or KOLIBRI_VOICE_API_KEY.",
        )
    if cfg.provider not in _SUPPORTED_VOICE_PROVIDERS:
        raise HTTPException(
            status_code=status.HTTP_400_BAD_REQUEST,
            detail=f"Unsupported voice provider: {cfg.provider}",
        )


def _decode_audio_base64(audio_base64: str) -> bytes:
    raw = audio_base64.strip()
    if raw.startswith("data:") and "," in raw:
        raw = raw.split(",", 1)[1]
    try:
        payload = base64.b64decode(raw, validate=True)
    except Exception as exc:  # noqa: BLE001
        raise HTTPException(status_code=400, detail=f"Invalid audio base64: {exc}") from exc
    if not payload:
        raise HTTPException(status_code=400, detail="Audio payload is empty")
    if len(payload) > _MAX_AUDIO_BYTES:
        raise HTTPException(
            status_code=status.HTTP_413_REQUEST_ENTITY_TOO_LARGE,
            detail=f"Audio payload exceeds limit ({_MAX_AUDIO_BYTES} bytes)",
        )
    return payload


def _audio_filename_for_mime(mime_type: str) -> str:
    ext = _MIME_TO_EXT.get(mime_type.lower().strip(), "webm")
    return f"voice_input.{ext}"


def _normalize_audio_format(audio_format: str) -> str:
    normalized = audio_format.lower().strip()
    if normalized == "pcm16":
        normalized = "wav"
    if normalized not in _AUDIO_MIME_BY_FORMAT:
        raise HTTPException(status_code=400, detail=f"Unsupported audio_format: {audio_format}")
    return normalized


def _extract_upstream_error(response: httpx.Response) -> str:
    try:
        payload = response.json()
        if isinstance(payload, dict):
            err = payload.get("error")
            if isinstance(err, dict) and isinstance(err.get("message"), str):
                return err["message"]
            if isinstance(payload.get("detail"), str):
                return payload["detail"]
    except Exception:  # noqa: BLE001
        pass
    body = response.text.strip()
    if body:
        return body[:400]
    return f"upstream status {response.status_code}"


async def _openai_transcribe(
    cfg: VoiceConfig,
    audio_bytes: bytes,
    mime_type: str,
    language: Optional[str],
    prompt: Optional[str] = None,
) -> tuple[str, Optional[str]]:
    filename = _audio_filename_for_mime(mime_type)
    headers = {"Authorization": f"Bearer {cfg.api_key}"}
    data = {"model": cfg.openai_transcribe_model}
    if language:
        data["language"] = language
    if prompt:
        data["prompt"] = prompt
    files = {"file": (filename, audio_bytes, mime_type or "application/octet-stream")}

    async with httpx.AsyncClient(timeout=cfg.timeout_sec) as client:
        response = await client.post(f"{cfg.openai_base_url}/audio/transcriptions", headers=headers, data=data, files=files)

    if response.status_code >= 400 and cfg.openai_transcribe_model != "whisper-1":
        # Фоллбек на whisper-1 для совместимости окружений.
        data["model"] = "whisper-1"
        async with httpx.AsyncClient(timeout=cfg.timeout_sec) as client:
            response = await client.post(
                f"{cfg.openai_base_url}/audio/transcriptions",
                headers=headers,
                data=data,
                files=files,
            )

    if response.status_code >= 400:
        raise HTTPException(
            status_code=status.HTTP_502_BAD_GATEWAY,
            detail=f"STT upstream error: {_extract_upstream_error(response)}",
        )

    try:
        payload = response.json()
    except Exception as exc:  # noqa: BLE001
        raise HTTPException(status_code=502, detail=f"STT invalid JSON: {exc}") from exc

    transcript = ""
    resolved_lang: Optional[str] = None
    if isinstance(payload, dict):
        maybe_text = payload.get("text")
        if isinstance(maybe_text, str):
            transcript = maybe_text.strip()
        maybe_lang = payload.get("language")
        if isinstance(maybe_lang, str) and maybe_lang.strip():
            resolved_lang = maybe_lang.strip()
    if not transcript:
        raise HTTPException(status_code=502, detail="STT upstream returned empty transcript")
    return transcript, resolved_lang


async def _openai_tts(
    cfg: VoiceConfig,
    text: str,
    voice: Optional[str],
    audio_format: str,
    speed: float,
) -> tuple[bytes, str, str]:
    fmt = _normalize_audio_format(audio_format)
    chosen_voice = (voice or cfg.default_voice).strip() or cfg.default_voice
    payload = {
        "model": cfg.openai_speech_model,
        "voice": chosen_voice,
        "input": text,
        "response_format": fmt,
        "speed": speed,
    }
    headers = {"Authorization": f"Bearer {cfg.api_key}"}
    async with httpx.AsyncClient(timeout=cfg.timeout_sec) as client:
        response = await client.post(f"{cfg.openai_base_url}/audio/speech", headers=headers, json=payload)

    if response.status_code >= 400:
        raise HTTPException(
            status_code=status.HTTP_502_BAD_GATEWAY,
            detail=f"TTS upstream error: {_extract_upstream_error(response)}",
        )

    audio = response.content
    if not audio:
        raise HTTPException(status_code=502, detail="TTS upstream returned empty audio")
    return audio, _AUDIO_MIME_BY_FORMAT[fmt], chosen_voice


def _gemini_extract_text(payload: dict) -> str:
    candidates = payload.get("candidates")
    if not isinstance(candidates, list) or not candidates:
        return ""
    content = candidates[0].get("content")
    if not isinstance(content, dict):
        return ""
    parts = content.get("parts")
    if not isinstance(parts, list):
        return ""
    chunks: list[str] = []
    for part in parts:
        if not isinstance(part, dict):
            continue
        text = part.get("text")
        if isinstance(text, str) and text.strip():
            chunks.append(text.strip())
    return "\n".join(chunks).strip()


def _parse_pcm_rate(mime_type: str) -> int:
    match = re.search(r"rate=(\d+)", mime_type or "", flags=re.IGNORECASE)
    if not match:
        return 24000
    try:
        value = int(match.group(1))
    except ValueError:
        return 24000
    return max(8000, min(96000, value))


def _pcm16_to_wav_bytes(pcm_bytes: bytes, sample_rate: int) -> bytes:
    buffer = io.BytesIO()
    with wave.open(buffer, "wb") as wav_file:
        wav_file.setnchannels(1)
        wav_file.setsampwidth(2)
        wav_file.setframerate(sample_rate)
        wav_file.writeframes(pcm_bytes)
    return buffer.getvalue()


def _resolve_gemini_voice(requested_voice: Optional[str], fallback: str) -> str:
    raw = (requested_voice or "").strip()
    if not raw:
        return fallback
    mapped = _OPENAI_TO_GEMINI_VOICE.get(raw.lower())
    if mapped:
        return mapped
    return raw


async def _gemini_transcribe(
    cfg: VoiceConfig,
    audio_bytes: bytes,
    mime_type: str,
    language: Optional[str],
    prompt: Optional[str] = None,
) -> tuple[str, Optional[str]]:
    instruction = (
        prompt.strip()
        if prompt and prompt.strip()
        else "Transcribe this audio accurately. Return only plain transcript text."
    )
    if language and language.strip():
        instruction = f"{instruction} Language hint: {language.strip()}."

    request_payload = {
        "contents": [
            {
                "parts": [
                    {"text": instruction},
                    {
                        "inline_data": {
                            "mime_type": (mime_type or "audio/webm").strip(),
                            "data": base64.b64encode(audio_bytes).decode("ascii"),
                        }
                    },
                ]
            }
        ],
        "generationConfig": {"temperature": 0},
    }

    async with httpx.AsyncClient(timeout=cfg.timeout_sec) as client:
        response = await client.post(
            f"{cfg.gemini_base_url}/models/{cfg.gemini_transcribe_model}:generateContent",
            params={"key": cfg.api_key},
            json=request_payload,
        )

    if response.status_code >= 400:
        err = _extract_upstream_error(response)
        if "location is not supported" in err.lower():
            _mark_voice_runtime_disabled("Gemini API geo restriction for server location")
            raise HTTPException(
                status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
                detail="Voice temporarily disabled: Gemini API geo restriction for server location.",
            )
        raise HTTPException(
            status_code=status.HTTP_502_BAD_GATEWAY,
            detail=f"STT upstream error: {err}",
        )

    try:
        payload = response.json()
    except Exception as exc:  # noqa: BLE001
        raise HTTPException(status_code=502, detail=f"STT invalid JSON: {exc}") from exc

    transcript = _gemini_extract_text(payload)
    if not transcript:
        raise HTTPException(status_code=502, detail="STT upstream returned empty transcript")
    return transcript, language


async def _gemini_tts(
    cfg: VoiceConfig,
    text: str,
    voice: Optional[str],
    _audio_format: str,
    _speed: float,
) -> tuple[bytes, str, str]:
    chosen_voice = _resolve_gemini_voice(voice, cfg.default_voice)
    request_payload = {
        "contents": [{"parts": [{"text": text}]}],
        "generationConfig": {
            "responseModalities": ["AUDIO"],
            "speechConfig": {
                "voiceConfig": {
                    "prebuiltVoiceConfig": {
                        "voiceName": chosen_voice,
                    }
                }
            },
            "temperature": 0,
        },
    }

    async with httpx.AsyncClient(timeout=cfg.timeout_sec) as client:
        response = await client.post(
            f"{cfg.gemini_base_url}/models/{cfg.gemini_speech_model}:generateContent",
            params={"key": cfg.api_key},
            json=request_payload,
        )

    if response.status_code >= 400:
        err = _extract_upstream_error(response)
        if "location is not supported" in err.lower():
            _mark_voice_runtime_disabled("Gemini API geo restriction for server location")
            raise HTTPException(
                status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
                detail="Voice temporarily disabled: Gemini API geo restriction for server location.",
            )
        raise HTTPException(
            status_code=status.HTTP_502_BAD_GATEWAY,
            detail=f"TTS upstream error: {err}",
        )

    try:
        payload = response.json()
    except Exception as exc:  # noqa: BLE001
        raise HTTPException(status_code=502, detail=f"TTS invalid JSON: {exc}") from exc

    candidates = payload.get("candidates")
    if not isinstance(candidates, list) or not candidates:
        raise HTTPException(status_code=502, detail="TTS upstream returned no candidates")
    content = candidates[0].get("content")
    if not isinstance(content, dict):
        raise HTTPException(status_code=502, detail="TTS upstream returned invalid content")
    parts = content.get("parts")
    if not isinstance(parts, list):
        raise HTTPException(status_code=502, detail="TTS upstream returned empty audio parts")

    for part in parts:
        if not isinstance(part, dict):
            continue
        inline_data = part.get("inlineData")
        if not isinstance(inline_data, dict):
            continue
        encoded_audio = inline_data.get("data")
        if not isinstance(encoded_audio, str) or not encoded_audio.strip():
            continue
        mime_type = str(inline_data.get("mimeType", "audio/wav"))
        try:
            raw_audio = base64.b64decode(encoded_audio, validate=True)
        except Exception as exc:  # noqa: BLE001
            raise HTTPException(status_code=502, detail=f"TTS invalid base64: {exc}") from exc
        if not raw_audio:
            continue
        if mime_type.lower().startswith("audio/l16"):
            sample_rate = _parse_pcm_rate(mime_type)
            wav_audio = _pcm16_to_wav_bytes(raw_audio, sample_rate=sample_rate)
            return wav_audio, "audio/wav", chosen_voice
        return raw_audio, mime_type, chosen_voice

    raise HTTPException(status_code=502, detail="TTS upstream returned empty audio")


async def _transcribe_audio(
    cfg: VoiceConfig,
    audio_bytes: bytes,
    mime_type: str,
    language: Optional[str],
    prompt: Optional[str] = None,
) -> tuple[str, Optional[str]]:
    if cfg.provider == "gemini":
        return await _gemini_transcribe(
            cfg=cfg,
            audio_bytes=audio_bytes,
            mime_type=mime_type,
            language=language,
            prompt=prompt,
        )
    return await _openai_transcribe(
        cfg=cfg,
        audio_bytes=audio_bytes,
        mime_type=mime_type,
        language=language,
        prompt=prompt,
    )


async def _synthesize_audio(
    cfg: VoiceConfig,
    text: str,
    voice: Optional[str],
    audio_format: str,
    speed: float,
) -> tuple[bytes, str, str]:
    if cfg.provider == "gemini":
        return await _gemini_tts(
            cfg=cfg,
            text=text,
            voice=voice,
            _audio_format=audio_format,
            _speed=speed,
        )
    return await _openai_tts(
        cfg=cfg,
        text=text,
        voice=voice,
        audio_format=audio_format,
        speed=speed,
    )


@router.get("/health", response_model=VoiceHealthResponse)
async def voice_health() -> VoiceHealthResponse:
    cfg = VoiceConfig.from_env()
    await _probe_gemini_runtime(cfg)
    runtime_disabled = _voice_runtime_disabled_reason()
    enabled = bool(cfg.api_key) and not runtime_disabled
    if runtime_disabled:
        detail = f"disabled: {runtime_disabled}"
    else:
        detail = "ok" if enabled else "Voice API key missing"
    return VoiceHealthResponse(
        enabled=enabled,
        provider=f"{cfg.provider}-audio",
        transcribe_model=cfg.transcribe_model,
        speech_model=cfg.speech_model,
        default_voice=cfg.default_voice,
        detail=detail,
    )


@router.post("/transcribe", response_model=VoiceTranscribeResponse)
async def transcribe_audio(req: VoiceTranscribeRequest) -> VoiceTranscribeResponse:
    cfg = VoiceConfig.from_env()
    _ensure_voice_enabled(cfg)
    audio_bytes = _decode_audio_base64(req.audio_base64)
    started = time.perf_counter()
    transcript, resolved_lang = await _transcribe_audio(
        cfg=cfg,
        audio_bytes=audio_bytes,
        mime_type=req.mime_type,
        language=req.language,
        prompt=req.prompt,
    )
    elapsed = (time.perf_counter() - started) * 1000.0
    return VoiceTranscribeResponse(transcript=transcript, language=resolved_lang or req.language, duration_ms=elapsed)


@router.post("/speak", response_model=VoiceSpeechResponse)
async def synthesize_speech(req: VoiceSpeechRequest) -> VoiceSpeechResponse:
    cfg = VoiceConfig.from_env()
    _ensure_voice_enabled(cfg)
    started = time.perf_counter()
    audio_bytes, audio_mime, chosen_voice = await _synthesize_audio(
        cfg=cfg,
        text=req.text,
        voice=req.voice,
        audio_format=req.audio_format,
        speed=req.speed,
    )
    elapsed = (time.perf_counter() - started) * 1000.0
    return VoiceSpeechResponse(
        audio_base64=base64.b64encode(audio_bytes).decode("ascii"),
        audio_mime=audio_mime,
        voice=chosen_voice,
        duration_ms=elapsed,
    )


@router.post("/chat-turn", response_model=VoiceChatTurnResponse)
async def voice_chat_turn(req: VoiceChatTurnRequest) -> VoiceChatTurnResponse:
    cfg = VoiceConfig.from_env()
    _ensure_voice_enabled(cfg)
    started = time.perf_counter()

    audio_bytes = _decode_audio_base64(req.audio_base64)
    transcript, _resolved_lang = await _transcribe_audio(
        cfg=cfg,
        audio_bytes=audio_bytes,
        mime_type=req.mime_type,
        language=req.language,
    )

    engine = get_engine()
    try:
        ai_result = await asyncio.to_thread(
            engine.chat,
            message=transcript,
            conversation_id=req.conversation_id,
            temperature=req.temperature,
        )
    except Exception as exc:  # noqa: BLE001
        raise HTTPException(status_code=500, detail=f"AI engine error: {exc}") from exc

    response_text = ai_result.get("response", "").strip() or "Не удалось сформировать ответ."
    audio_base64: Optional[str] = None
    audio_mime: Optional[str] = None
    chosen_voice: Optional[str] = None

    if req.speak and response_text:
        try:
            tts_bytes, tts_mime, chosen_voice = await _synthesize_audio(
                cfg=cfg,
                text=response_text,
                voice=req.voice,
                audio_format=req.audio_format,
                speed=req.speed,
            )
            audio_base64 = base64.b64encode(tts_bytes).decode("ascii")
            audio_mime = tts_mime
        except HTTPException as exc:
            # Не роняем весь voice turn, если TTS-провайдер временно недоступен:
            # клиент получит текст ответа и сможет озвучить его в браузере.
            if exc.status_code >= 500:
                audio_base64 = None
                audio_mime = None
                chosen_voice = None
            else:
                raise

    elapsed = (time.perf_counter() - started) * 1000.0
    return VoiceChatTurnResponse(
        transcript=transcript,
        response=response_text,
        conversation_id=ai_result.get("conversation_id", req.conversation_id or ""),
        confidence=float(ai_result.get("confidence", 0.0)),
        duration_ms=elapsed,
        audio_base64=audio_base64,
        audio_mime=audio_mime,
        voice=chosen_voice,
        method=str(ai_result.get("method", "voice-chat")),
    )
