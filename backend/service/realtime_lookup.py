"""Realtime lookup helpers for weather, news, time, rates, and reference facts."""
from __future__ import annotations

from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime
from email.utils import parsedate_to_datetime
import html
import json
import re
import threading
import time
from typing import Any
import unicodedata
from urllib.parse import parse_qs, quote, urlparse
import xml.etree.ElementTree as ET

import requests

try:
    from zoneinfo import ZoneInfo
except ImportError:  # pragma: no cover
    ZoneInfo = None  # type: ignore[assignment]

_HEADERS = {
    "User-Agent": "KolibriAI/1.0 (+https://kolibriai.ru)",
    "Accept": "application/json, text/xml, application/xml, text/html;q=0.8, */*;q=0.5",
    "Accept-Language": "ru,en;q=0.8",
}

_WEATHER_CODE_LABELS_RU: dict[int, str] = {
    0: "ясно",
    1: "преимущественно ясно",
    2: "переменная облачность",
    3: "пасмурно",
    45: "туман",
    48: "изморозь",
    51: "слабая морось",
    53: "морось",
    55: "сильная морось",
    56: "слабая ледяная морось",
    57: "ледяная морось",
    61: "слабый дождь",
    63: "дождь",
    65: "сильный дождь",
    66: "слабый ледяной дождь",
    67: "ледяной дождь",
    71: "слабый снег",
    73: "снег",
    75: "сильный снег",
    77: "снежные зёрна",
    80: "кратковременный дождь",
    81: "ливень",
    82: "сильный ливень",
    85: "снежные заряды",
    86: "сильные снежные заряды",
    95: "гроза",
    96: "гроза с градом",
    99: "сильная гроза с градом",
}

_RSS_BAD_TITLE_PREFIXES = (
    "Bing:",
    "Microsoft Bing",
)

_RSS_BAD_TITLE_SNIPPETS = (
    "wikipedia",
    "википедия",
)

_RSS_BAD_DOMAINS = (
    "wikipedia.org",
    "wikidata.org",
)

_CURATED_WORLD_NEWS_FEEDS: tuple[dict[str, str], ...] = (
    {"source": "Lenta.ru Мир", "url": "https://lenta.ru/rss/news/world", "priority": "8"},
    {"source": "РБК Мир", "url": "https://rssexport.rbc.ru/rbcnews/news/30/full.rss", "priority": "7"},
    {"source": "РИА Новости", "url": "https://ria.ru/export/rss2/archive/index.xml", "priority": "5"},
    {"source": "UN News", "url": "https://news.un.org/feed/subscribe/ru/news/all/rss.xml", "priority": "5"},
)

_RSS_HTTP_CACHE_TTL_SECONDS = 180.0
_RSS_HTTP_CACHE: dict[tuple[str, tuple[tuple[str, str], ...]], tuple[float, str]] = {}
_RSS_HTTP_CACHE_LOCK = threading.Lock()
_NEWS_DIGEST_CACHE_TTL_SECONDS = 90.0
_NEWS_DIGEST_CACHE: dict[tuple[str, int], tuple[float, str]] = {}
_NEWS_DIGEST_CACHE_LOCK = threading.Lock()
_REFERENCE_CACHE_TTL_SECONDS = 43200.0
_REFERENCE_CACHE: dict[str, tuple[float, str]] = {}
_REFERENCE_CACHE_LOCK = threading.Lock()

_CURRENCY_ALIASES: dict[str, tuple[str, ...]] = {
    "USD": ("usd", "доллар", "доллара", "долларов", "бакс", "бакса", "баксов", "$"),
    "EUR": ("eur", "евро", "euro"),
    "RUB": ("rub", "rur", "рубль", "рубля", "рублей", "руб", "₽"),
    "GBP": ("gbp", "фунт", "фунта", "фунтов", "pound", "pounds"),
    "JPY": ("jpy", "иена", "йена", "иены", "йены", "yen", "yens"),
    "CNY": ("cny", "юань", "юаня", "юаней", "yuan", "yuans"),
    "KZT": ("kzt", "тенге"),
    "TRY": ("try", "лира", "лиры", "лир", "lira", "liras"),
    "AED": ("aed", "дирхам", "дирхама", "дирхамов", "dirham", "dirhams"),
    "BYN": ("byn", "белорусский рубль", "белорусских рублей"),
}

_TIME_QUERY_MARKERS = (
    "который час",
    "сколько времени",
    "скока времени",
    "время в",
    "time in",
    "local time",
    "what time",
)

_REFERENCE_QUERY_MARKERS = (
    "что такое",
    "кто такой",
    "кто такая",
    "где находится",
    "где расположен",
    "когда род",
    "какая столица",
    "столица ",
    "capital of",
    "what is",
    "who is",
    "where is",
)

_NEWS_DIVERSITY_STOPWORDS = {
    "новости",
    "мира",
    "мире",
    "сегодня",
    "главные",
    "последние",
    "события",
    "дня",
    "день",
    "утро",
    "вечер",
    "неделя",
    "week",
    "world",
    "news",
    "latest",
    "global",
}


def _collapse_spaces(text: str) -> str:
    return re.sub(r"\s+", " ", str(text or "").strip())


def _contains_cyrillic(text: str) -> bool:
    return any("\u0400" <= c <= "\u04ff" for c in str(text or ""))


def _normalize_key(text: str) -> str:
    return re.sub(r"[^a-zа-яё0-9]+", " ", _collapse_spaces(text).lower()).strip()


def _fold_diacritics(text: str) -> str:
    normalized = unicodedata.normalize("NFKD", str(text or ""))
    return "".join(char for char in normalized if not unicodedata.combining(char))


def _unique_keep_order(items: list[str]) -> list[str]:
    out: list[str] = []
    seen: set[str] = set()
    for item in items:
        key = item.lower()
        if not item or key in seen:
            continue
        seen.add(key)
        out.append(item)
    return out


def _city_case_variants(token: str) -> list[str]:
    low = _collapse_spaces(token).lower()
    if len(low) < 3:
        return [low]
    variants = [low]
    if low.endswith("ске") and len(low) >= 5:
        variants.append(low[:-1])
    if low.endswith("бурге") and len(low) >= 7:
        variants.append(low[:-1])
    if low.endswith("граде") and len(low) >= 6:
        variants.append(low[:-1])
    if low.endswith(("е", "и", "у", "ю")) and len(low) >= 5:
        root = low[:-1]
        variants.extend([root, root + "а"])
    return _unique_keep_order([v for v in variants if len(v) >= 3])


def _location_variants(location_hint: str) -> list[str]:
    base = _collapse_spaces(location_hint)
    if not base:
        return []
    tokens = base.split()
    variants = [base]
    if tokens:
        last = tokens[-1]
        for alt_last in _city_case_variants(last):
            variants.append(" ".join(tokens[:-1] + [alt_last]).strip())
    if len(tokens) == 1:
        variants.extend(_city_case_variants(tokens[0]))
    return _unique_keep_order([v for v in variants if v])


def _weather_label(code: Any) -> str:
    try:
        return _WEATHER_CODE_LABELS_RU.get(int(code), "неуточнённая погода")
    except Exception:
        return "неуточнённая погода"


def _resolve_place(location_hint: str, *, timeout: float = 5.5, language: str = "ru") -> dict[str, Any] | None:
    variants = _location_variants(location_hint)
    if not variants:
        return None

    for variant in variants:
        try:
            resp = requests.get(
                "https://geocoding-api.open-meteo.com/v1/search",
                params={
                    "name": variant,
                    "count": 5,
                    "language": language,
                    "format": "json",
                },
                headers=_HEADERS,
                timeout=max(1.8, timeout * 0.45),
            )
            resp.raise_for_status()
            data = resp.json()
        except Exception:
            continue
        results = list(data.get("results") or [])
        if not results:
            continue
        results.sort(
            key=lambda item: (
                str(item.get("country_code") or "") != "RU",
                -int(item.get("population") or 0),
            ),
        )
        return results[0]
    return None


def _place_label(place: dict[str, Any], fallback: str) -> str:
    city = _collapse_spaces(place.get("name") or fallback)
    admin1 = _collapse_spaces(place.get("admin1") or "")
    country = _collapse_spaces(place.get("country") or "")
    parts = [city]
    if admin1 and admin1.lower() != city.lower():
        parts.append(admin1)
    if country and country.lower() not in {city.lower(), admin1.lower()}:
        parts.append(country)
    return ", ".join(parts)


def fetch_weather_answer(
    location_hint: str,
    *,
    timeout: float = 5.5,
    language: str = "ru",
) -> str | None:
    place = _resolve_place(location_hint, timeout=timeout, language=language)
    if not place:
        return None

    try:
        forecast = requests.get(
            "https://api.open-meteo.com/v1/forecast",
            params={
                "latitude": place["latitude"],
                "longitude": place["longitude"],
                "current": ",".join([
                    "temperature_2m",
                    "apparent_temperature",
                    "precipitation",
                    "weather_code",
                    "wind_speed_10m",
                ]),
                "daily": ",".join([
                    "weather_code",
                    "temperature_2m_max",
                    "temperature_2m_min",
                    "precipitation_probability_max",
                ]),
                "timezone": "auto",
                "forecast_days": 1,
            },
            headers=_HEADERS,
            timeout=max(2.0, timeout),
        )
        forecast.raise_for_status()
        data = forecast.json()
    except Exception:
        return None

    current = dict(data.get("current") or {})
    daily = dict(data.get("daily") or {})
    location_text = _place_label(place, location_hint)

    current_desc = _weather_label(current.get("weather_code"))
    daily_codes = list(daily.get("weather_code") or [])
    daily_desc = _weather_label(daily_codes[0]) if daily_codes else current_desc
    try:
        t_now = round(float(current.get("temperature_2m")), 1)
        feels = round(float(current.get("apparent_temperature")), 1)
        wind = round(float(current.get("wind_speed_10m")), 1)
        precipitation = round(float(current.get("precipitation")), 1)
    except Exception:
        return None

    daily_max = (daily.get("temperature_2m_max") or [None])[0]
    daily_min = (daily.get("temperature_2m_min") or [None])[0]
    rain_prob = (daily.get("precipitation_probability_max") or [None])[0]

    answer = (
        f"Сейчас в {location_text} {current_desc}: {t_now} °C, "
        f"ощущается как {feels} °C, ветер {wind} км/ч."
    )
    if precipitation and precipitation > 0:
        answer += f" Осадки сейчас около {precipitation} мм."
    if daily_min is not None and daily_max is not None:
        answer += (
            f" Сегодня прогноз: {daily_desc}, "
            f"от {round(float(daily_min), 1)} до {round(float(daily_max), 1)} °C."
        )
    if rain_prob is not None:
        answer += f" Вероятность осадков до {int(round(float(rain_prob)))}%."
    return answer


def _extract_domain(url: str) -> str:
    try:
        host = (urlparse(url).netloc or "").replace("www.", "").lower()
        parts = [part for part in host.split(".") if part]
        if len(parts) >= 3 and parts[-2] in {"co", "com", "org", "net"} and len(parts[-1]) == 2:
            return ".".join(parts[-3:])
        if len(parts) >= 2:
            return ".".join(parts[-2:])
        return host
    except Exception:
        return ""


def _unwrap_bing_redirect(link: str) -> str:
    try:
        parsed = urlparse(link)
        if "bing.com" not in (parsed.netloc or "").lower():
            return link
        query = parse_qs(parsed.query)
        target = _collapse_spaces((query.get("url") or [""])[0])
        return target or link
    except Exception:
        return link


def _strip_html_tags(text: str) -> str:
    clean = re.sub(r"<[^>]+>", " ", str(text or ""))
    return _collapse_spaces(html.unescape(clean))


def _http_cache_key(url: str, params: dict[str, Any] | None = None) -> tuple[str, tuple[tuple[str, str], ...]]:
    normalized_params = tuple(
        sorted((str(key), _collapse_spaces(value)) for key, value in dict(params or {}).items())
    )
    return (url, normalized_params)


def _http_get_text_cached(
    url: str,
    *,
    params: dict[str, Any] | None = None,
    timeout: float,
) -> str | None:
    key = _http_cache_key(url, params)
    now = time.monotonic()
    with _RSS_HTTP_CACHE_LOCK:
        cached = _RSS_HTTP_CACHE.get(key)
        if cached and (now - cached[0]) <= _RSS_HTTP_CACHE_TTL_SECONDS:
            return cached[1]
    try:
        resp = requests.get(
            url,
            params=params,
            headers=_HEADERS,
            timeout=max(1.2, timeout),
        )
        resp.raise_for_status()
        text = resp.text
    except Exception:
        with _RSS_HTTP_CACHE_LOCK:
            cached = _RSS_HTTP_CACHE.get(key)
        return cached[1] if cached else None
    with _RSS_HTTP_CACHE_LOCK:
        _RSS_HTTP_CACHE[key] = (now, text)
    return text


def _generic_news_item_penalty(title: str, description: str, link: str) -> float:
    title_key = _normalize_key(title)
    desc_key = _normalize_key(description)
    generic_terms = {
        "новости", "мира", "мире", "сегодня", "главные", "свежие",
        "latest", "world", "news", "headlines", "breaking", "international",
        "global", "top",
    }
    title_tokens = [t for t in title_key.split() if t]
    desc_tokens = [t for t in desc_key.split() if t]
    specific_title = [t for t in title_tokens if t not in generic_terms]
    specific_desc = [t for t in desc_tokens if t not in generic_terms]
    path = urlparse(link).path.strip("/")

    penalty = 0.0
    if len(specific_title) < 2:
        penalty += 16.0
    if len(specific_desc) < 4:
        penalty += 8.0
    if not path or path in {"news", "world", "latest-news"} or path.count("/") < 1:
        penalty += 10.0
    return penalty


def _news_specificity_bonus(title: str, description: str) -> float:
    title_key = _normalize_key(title)
    desc_key = _normalize_key(description)
    generic_terms = {
        "новости", "мира", "мире", "сегодня", "главные", "свежие",
        "latest", "world", "news", "headlines", "breaking", "international",
        "global", "top",
    }
    title_tokens = [t for t in title_key.split() if t and t not in generic_terms]
    desc_tokens = [t for t in desc_key.split() if t and t not in generic_terms]
    bonus = min(10.0, len(title_tokens) * 1.8 + min(4, len(desc_tokens)) * 0.8)
    if re.search(r"\b\d{1,4}\b", title) or re.search(r"\b\d{1,4}\b", description):
        bonus += 2.0
    return bonus


def _news_editorial_penalty(title: str, description: str) -> float:
    title_key = _normalize_key(title)
    text_key = _normalize_key(f"{title} {description}")
    penalty = 0.0

    if "?" in title or "!" in title:
        penalty += 6.0
    if re.match(r"^(как|почему|зачем|что|какой|какая|какие|когда)\b", title_key):
        penalty += 6.0

    digest_markers = (
        "утренний выпуск",
        "вечерний выпуск",
        "дневной выпуск",
        "новости дня",
    )
    if any(marker in title_key for marker in digest_markers):
        penalty += 5.0

    soft_markers = (
        "праздник",
        "традици",
        "примет",
        "рейтинг",
        "обзор",
        "подборк",
        "гайд",
        "guide",
        "tips",
        "совет",
        "турист",
        "курорт",
        "retreat",
        "wellness",
    )
    if any(marker in text_key for marker in soft_markers):
        penalty += 8.0

    product_markers = (
        "анонсиров",
        "выпустил",
        "выпустила",
        "презентац",
        "смартфон",
        "видеокарт",
        "видеокарта",
        "playstation",
        "xbox",
        "nintendo",
        "world of warcraft",
    )
    if any(marker in text_key for marker in product_markers):
        penalty += 10.0

    sports_markers = (
        "матч",
        "нхл",
        "футбол",
        "хокке",
        "теннис",
        "чемпионат",
        "кубок",
        "лыж",
        "гимнаст",
        "олимпиад",
        "сборн",
        "тренер",
        "спорт",
    )
    if any(marker in text_key for marker in sports_markers):
        penalty += 18.0

    return penalty


def _news_aggregator_penalty(source: str, domain: str, title: str) -> float:
    source_key = _normalize_key(source)
    domain_key = _collapse_spaces(domain).lower()
    penalty = 0.0

    aggregator_domains = (
        "mail.ru",
        "news.mail.ru",
        "rambler.ru",
        "news.rambler.ru",
        "smi2.ru",
    )
    aggregator_sources = (
        "новости mail ru",
        "mail ru",
        "рамблер",
        "rambler",
        "smi2",
    )
    if any(domain_key.endswith(marker) for marker in aggregator_domains):
        penalty += 9.0
    if any(marker in source_key for marker in aggregator_sources):
        penalty += 9.0
    if penalty and _is_digest_like_news_title(title):
        penalty += 4.0
    return penalty


def _news_eventworthiness_bonus(title: str, description: str) -> float:
    text_key = _normalize_key(f"{title} {description}")
    bonus = 0.0

    event_markers = (
        "саммит",
        "переговор",
        "санкц",
        "президент",
        "премьер",
        "министр",
        "правительств",
        "совбез",
        "оон",
        "выбор",
        "закон",
        "соглашен",
        "перемир",
        "армия",
        "военн",
        "удар",
        "атак",
        "суд",
        "кризис",
        "инфляц",
        "ставк",
        "рынк",
        "экономик",
        "землетряс",
        "вулкан",
        "катастроф",
    )
    if any(marker in text_key for marker in event_markers):
        bonus += 6.0

    if re.search(
        r"\b(согласовал|объявил|объявила|принял|приняла|одобрил|одобрила|утвердил|утвердила|начал|начала|произошл|провел|провела|ввел|ввела|обсудил|обсудила)\b",
        text_key,
    ):
        bonus += 4.0

    digest_markers = (
        "ключевые события",
        "дайджест",
        "главные новости к",
        "главные события",
    )
    if any(marker in text_key for marker in digest_markers):
        bonus += 3.0

    return bonus


def _news_diversity_terms(title: str, description: str = "") -> set[str]:
    text_key = _normalize_key(f"{title} {description}")
    out: set[str] = set()
    for token in text_key.split():
        if len(token) < 4 or token.isdigit() or token in _NEWS_DIVERSITY_STOPWORDS:
            continue
        out.add(token)
    return out


def _parse_news_pubdate(raw: str) -> datetime | None:
    try:
        parsed = parsedate_to_datetime(_collapse_spaces(raw))
    except Exception:
        return None
    if parsed is None:
        return None
    if parsed.tzinfo is None:
        return parsed
    return parsed.astimezone()


def _news_recency_bonus(published_at: datetime | None) -> float:
    if published_at is None:
        return 0.0
    now = datetime.now(published_at.tzinfo) if published_at.tzinfo else datetime.now()
    age_hours = max(0.0, (now - published_at).total_seconds() / 3600.0)
    if age_hours <= 12:
        return 12.0
    if age_hours <= 24:
        return 8.0
    if age_hours <= 48:
        return 4.0
    if age_hours <= 96:
        return 1.0
    if age_hours <= 168:
        return -4.0
    return -8.0


def _xml_item_text(item: ET.Element, local_name: str) -> str:
    direct = item.findtext(local_name)
    if direct:
        return direct
    for child in list(item):
        tag = str(getattr(child, "tag", "") or "")
        if tag == local_name or tag.endswith(f"}}{local_name}") or tag.endswith(local_name):
            return str(child.text or "")
    return ""


def fetch_curated_world_news_items(
    *,
    timeout: float = 5.0,
    max_items: int = 3,
) -> list[dict[str, str]]:
    def _best_item_for_feed(feed: dict[str, str]) -> tuple[float, dict[str, str]] | None:
        xml_text = _http_get_text_cached(feed["url"], timeout=min(timeout, 3.2))
        if not xml_text:
            return None
        try:
            root = ET.fromstring(xml_text)
        except Exception:
            return None

        feed_candidates: list[tuple[float, dict[str, str]]] = []
        for item in root.findall(".//item"):
            title = _strip_html_tags(_xml_item_text(item, "title"))
            desc = _strip_html_tags(_xml_item_text(item, "description"))
            link = _collapse_spaces(_xml_item_text(item, "link"))
            published_raw = _collapse_spaces(_xml_item_text(item, "pubDate") or _xml_item_text(item, "date"))
            if not title or not link:
                continue
            domain = _extract_domain(link)
            if any(domain.endswith(bad) for bad in _RSS_BAD_DOMAINS):
                continue
            recency = _news_recency_bonus(_parse_news_pubdate(published_raw))
            editorial = _news_editorial_penalty(title, desc)
            score = (
                100.0
                + _news_specificity_bonus(title, desc)
                + _news_eventworthiness_bonus(title, desc)
                + recency
                - editorial
                + float(feed.get("priority") or 0.0)
            )
            if editorial >= 18.0 or score < 101.0:
                continue
            feed_candidates.append(
                (
                    score,
                    {
                        "title": title,
                        "description": desc,
                        "link": link,
                        "domain": domain,
                        "source": feed["source"],
                        "published_at": published_raw,
                        "score": f"{score:.2f}",
                    },
                )
            )
        if not feed_candidates:
            return None
        feed_candidates.sort(key=lambda row: row[0], reverse=True)
        return feed_candidates[0]

    out: list[dict[str, str]] = []
    seen: set[str] = set()
    with ThreadPoolExecutor(max_workers=min(4, len(_CURATED_WORLD_NEWS_FEEDS) or 1)) as pool:
        futures = [pool.submit(_best_item_for_feed, feed) for feed in _CURATED_WORLD_NEWS_FEEDS]
        ranked: list[tuple[float, dict[str, str]]] = []
        for future in as_completed(futures):
            result = future.result()
            if result:
                ranked.append(result)

    ranked.sort(key=lambda row: row[0], reverse=True)
    for _, item in ranked:
        key = _normalize_key(item["title"])
        if not key or key in seen:
            continue
        seen.add(key)
        out.append(item)
        if len(out) >= max(1, max_items):
            break
    return out


def fetch_bing_search_rss(
    query: str,
    *,
    timeout: float = 5.0,
    max_items: int = 5,
) -> list[dict[str, str]]:
    q = _collapse_spaces(query)
    if not q:
        return []
    xml_text = _http_get_text_cached(
        "https://www.bing.com/news/search",
        params={"q": q, "format": "rss"},
        timeout=max(1.5, timeout),
    )
    if not xml_text:
        return []
    try:
        root = ET.fromstring(xml_text)
    except Exception:
        return []

    channel = root.find("channel")
    if channel is None:
        return []
    out: list[dict[str, str]] = []
    seen: set[str] = set()
    for item in channel.findall("item"):
        title = _collapse_spaces(html.unescape(item.findtext("title") or ""))
        desc = _collapse_spaces(html.unescape(item.findtext("description") or ""))
        raw_link = _collapse_spaces(item.findtext("link") or "")
        link = _unwrap_bing_redirect(raw_link)
        published_raw = _collapse_spaces(item.findtext("pubDate") or "")
        source = ""
        for child in list(item):
            if str(getattr(child, "tag", "") or "").endswith("Source"):
                source = _collapse_spaces(html.unescape(child.text or ""))
                if source:
                    break
        if not title or not link:
            continue
        if title.startswith(_RSS_BAD_TITLE_PREFIXES):
            continue
        lowered_title = title.lower()
        if any(snippet in lowered_title for snippet in _RSS_BAD_TITLE_SNIPPETS):
            continue
        domain = _extract_domain(link)
        if any(domain.endswith(bad) for bad in _RSS_BAD_DOMAINS):
            continue
        key = _normalize_key(title)
        if not key or key in seen:
            continue
        seen.add(key)
        out.append({
            "title": title,
            "description": desc,
            "link": link,
            "domain": domain,
            "source": source,
            "published_at": published_raw,
        })
        if len(out) >= max(1, max_items):
            break
    return out


def _is_generic_news_query(query: str) -> bool:
    low = _collapse_spaces(query).lower()
    generic_markers = (
        "мировые новости",
        "новости в мире",
        "новости мира",
        "международные новости",
        "главные новости",
        "world news",
        "international news",
        "global news",
        "latest news",
    )
    return any(marker in low for marker in generic_markers)


def _news_query_variants(query: str) -> list[str]:
    q = _collapse_spaces(query)
    variants = [q]
    low = q.lower()
    has_cyr = _contains_cyrillic(q)
    if has_cyr and _is_generic_news_query(q):
        variants.extend([
            "ключевые события дня в мире",
            "главные события в мире за день",
            "последние новости мира за день",
            "международные новости сегодня",
        ])
    elif (not has_cyr) and _is_generic_news_query(q):
        variants.extend([
            "world news today",
            "international news today",
            "top global news today",
        ])
    elif has_cyr and ("тех" in low or "ии" in low or "ai" in low):
        variants.extend([
            "новости технологий сегодня",
            "новости искусственного интеллекта сегодня",
        ])
    elif (not has_cyr) and ("tech" in low or "ai" in low):
        variants.extend([
            "technology news today",
            "artificial intelligence news today",
        ])
    return _unique_keep_order([v for v in variants if v])


def _is_digest_like_news_title(title: str) -> bool:
    key = _normalize_key(title)
    markers = (
        "главные новости",
        "ключевые события",
        "последние новости",
        "новости россии и мира",
        "world news in brief",
    )
    return any(marker in key for marker in markers)


def _trim_news_clause(text: str, *, max_len: int = 96) -> str:
    src = _collapse_spaces(text)
    if not src:
        return ""
    src = re.sub(r"\s*\.\.\.\s*$", "", src)
    parts = re.split(r"(?<=[.!?])\s+|\s+[|/]\s+|\s+—\s+", src)
    candidate = _collapse_spaces(parts[0] if parts else src)
    if len(candidate) > max_len:
        candidate = candidate[: max_len - 3].rstrip(" ,;:") + "..."
    return candidate


def _pick_news_desc_clause(title: str, description: str, *, max_len: int) -> str:
    raw_parts = re.split(r"(?<=[.!?])\s+|\s+—\s+", _collapse_spaces(description))
    parts = [_collapse_spaces(part.strip(" .")) for part in raw_parts if _collapse_spaces(part.strip(" ."))]
    if not parts:
        return ""
    title_tokens = set(_normalize_key(title).split())
    bad_prefixes = (
        "дайджест",
        "последние новости",
        "рассказываем",
        "читайте",
    )
    for part in parts:
        part_key = _normalize_key(part)
        if not part_key or any(part_key.startswith(prefix) for prefix in bad_prefixes):
            continue
        part_tokens = set(part_key.split())
        if part_tokens and len(part_tokens - title_tokens) < 2:
            continue
        return _trim_news_clause(part, max_len=max_len)
    return _trim_news_clause(parts[0], max_len=max_len)


def _compact_news_blurb(title: str, description: str, *, is_generic: bool) -> str:
    desc = _trim_news_clause(description, max_len=88 if is_generic else 96)
    if not desc:
        return ""
    if _is_digest_like_news_title(title):
        return _pick_news_desc_clause(title, description, max_len=72 if is_generic else 90)
    if is_generic:
        return ""
    return desc


def fetch_news_digest(
    query: str,
    *,
    timeout: float = 5.0,
    max_items: int = 3,
) -> str | None:
    cache_key = (_normalize_key(query), max(1, int(max_items or 1)))
    now = time.monotonic()
    with _NEWS_DIGEST_CACHE_LOCK:
        cached = _NEWS_DIGEST_CACHE.get(cache_key)
        if cached and (now - cached[0]) <= _NEWS_DIGEST_CACHE_TTL_SECONDS:
            return cached[1]

    variants = _news_query_variants(query)
    is_generic = _is_generic_news_query(query)
    candidates: list[dict[str, Any]] = []
    seen_titles: set[str] = set()

    if is_generic:
        curated_pool_size = max(6, max_items * 2)
        for item in fetch_curated_world_news_items(timeout=min(timeout, 3.2), max_items=curated_pool_size):
            title = _collapse_spaces(item.get("title") or "")
            key = _normalize_key(title)
            if not title or not key or key in seen_titles:
                continue
            seen_titles.add(key)
            candidates.append(
                {
                    "title": title,
                    "description": _collapse_spaces(item.get("description") or ""),
                    "link": _collapse_spaces(item.get("link") or ""),
                    "domain": _collapse_spaces(item.get("domain") or ""),
                    "source": _collapse_spaces(item.get("source") or ""),
                    "published_at": _collapse_spaces(item.get("published_at") or ""),
                    "score": float(item.get("score") or 0.0),
                }
            )

    need_bing_supplement = (not is_generic) or len(candidates) < max(1, max_items)
    if need_bing_supplement:
        search_variants = variants
        if is_generic and candidates:
            search_variants = variants[:2]
        with ThreadPoolExecutor(max_workers=min(4, len(search_variants) or 1)) as pool:
            futures = {
                pool.submit(
                    fetch_bing_search_rss,
                    variant,
                    timeout=max(1.8, min(timeout, 3.2)) if is_generic else timeout,
                    max_items=max(5, max_items * 3),
                ): (variant_idx, variant)
                for variant_idx, variant in enumerate(search_variants)
            }
            variant_results: list[tuple[int, list[dict[str, str]]]] = []
            for future in as_completed(futures):
                variant_idx, _variant = futures[future]
                try:
                    variant_results.append((variant_idx, future.result()))
                except Exception:
                    continue
        for variant_idx, items in sorted(variant_results, key=lambda row: row[0]):
            for item_idx, item in enumerate(items):
                title = _collapse_spaces(item.get("title") or "")
                desc = _collapse_spaces(item.get("description") or "")
                link = _collapse_spaces(item.get("link") or "")
                domain = _collapse_spaces(item.get("domain") or _extract_domain(link))
                source = _collapse_spaces(item.get("source") or "")
                published_at = _parse_news_pubdate(str(item.get("published_at") or ""))
                key = _normalize_key(title)
                if not title or not link or not key or key in seen_titles:
                    continue
                seen_titles.add(key)
                candidates.append(
                    {
                        "title": title,
                        "description": desc,
                        "link": link,
                        "domain": domain,
                        "source": source,
                        "published_at": str(item.get("published_at") or ""),
                        "score": (
                            100.0
                            - variant_idx * (4.0 if is_generic else 12.0)
                            - item_idx * 1.1
                            - (_generic_news_item_penalty(title, desc, link) if is_generic else 0.0)
                            - (_news_editorial_penalty(title, desc) if is_generic else 0.0)
                            - (_news_aggregator_penalty(source, domain, title) if is_generic else 0.0)
                            + _news_specificity_bonus(title, desc)
                            + (_news_eventworthiness_bonus(title, desc) if is_generic else 0.0)
                            + _news_recency_bonus(published_at)
                        ),
                    }
                )

    if not candidates:
        return None

    candidates.sort(key=lambda item: float(item["score"]), reverse=True)
    selected: list[dict[str, Any]] = []
    selected_keys: set[str] = set()
    domain_counts: Counter[str] = Counter()
    selected_topic_terms: list[set[str]] = []
    domain_cap = 1 if is_generic else 2
    deferred_duplicates: list[dict[str, Any]] = []

    for item in candidates:
        key = _normalize_key(str(item.get("title") or ""))
        if not key or key in selected_keys:
            continue
        domain = str(item.get("domain") or "")
        if domain and domain_counts[domain] >= domain_cap:
            continue
        if is_generic:
            topic_terms = _news_diversity_terms(
                str(item.get("title") or ""),
                str(item.get("description") or ""),
            )
            if topic_terms and any(len(topic_terms & prev_terms) >= 3 for prev_terms in selected_topic_terms):
                deferred_duplicates.append(item)
                continue
        selected.append(item)
        selected_keys.add(key)
        if domain:
            domain_counts[domain] += 1
        if is_generic:
            selected_topic_terms.append(
                _news_diversity_terms(
                    str(item.get("title") or ""),
                    str(item.get("description") or ""),
                )
            )
        if len(selected) >= max(1, max_items):
            break

    if is_generic and len(selected) < max(1, max_items):
        for item in deferred_duplicates:
            key = _normalize_key(str(item.get("title") or ""))
            if not key or key in selected_keys:
                continue
            domain = str(item.get("domain") or "")
            if domain and domain_counts[domain] >= domain_cap:
                continue
            selected.append(item)
            selected_keys.add(key)
            if domain:
                domain_counts[domain] += 1
            if len(selected) >= max(1, max_items):
                break

    if (not is_generic) and len(selected) < max(1, max_items):
        for item in candidates:
            key = _normalize_key(str(item.get("title") or ""))
            if not key or key in selected_keys:
                continue
            selected.append(item)
            selected_keys.add(key)
            if len(selected) >= max(1, max_items):
                break

    if is_generic and selected:
        top_score = float(selected[0].get("score") or 0.0)
        cutoff = max(84.0, top_score - 24.0)
        filtered = [selected[0]]
        filtered.extend(
            item for item in selected[1:]
            if float(item.get("score") or 0.0) >= cutoff
        )
        selected = filtered

    lines: list[str] = []
    for idx, item in enumerate(selected[: max(1, max_items)], start=1):
        title = _collapse_spaces(item.get("title") or "")
        desc = _collapse_spaces(item.get("description") or "")
        domain = _collapse_spaces(item.get("domain") or "")
        source = _collapse_spaces(item.get("source") or "")
        desc = _compact_news_blurb(title, desc, is_generic=is_generic)
        line = f"{idx}. {title}"
        if desc and desc.lower() not in title.lower():
            line += f" — {desc}"
        if source:
            line += f" ({source})"
        elif domain:
            line += f" ({domain})"
        lines.append(line)
    if not lines:
        return None
    prefix = "Сейчас по теме" if is_generic else "Вот что нашёл по теме"
    answer = f"{prefix} «{_collapse_spaces(query)}»: " + " ".join(lines)
    with _NEWS_DIGEST_CACHE_LOCK:
        _NEWS_DIGEST_CACHE[cache_key] = (now, answer)
        if len(_NEWS_DIGEST_CACHE) > 128:
            stale = sorted(_NEWS_DIGEST_CACHE.items(), key=lambda item: item[1][0])[:32]
            for key, _value in stale:
                _NEWS_DIGEST_CACHE.pop(key, None)
    return answer


def looks_like_time_query(query: str) -> bool:
    low = _collapse_spaces(query).lower()
    if not low:
        return False
    if any(marker in low for marker in _TIME_QUERY_MARKERS):
        return True
    return bool(re.search(r"\b(time|время|час)\b", low) and re.search(r"\b(в|во|in)\b", low))


def _extract_time_location_hint(query: str) -> str:
    src = _collapse_spaces(query)
    if not src:
        return ""
    patterns = (
        r"(?:сколько\s+времени|который\s+час|время)\s+(?:сейчас\s+)?(?:в|во)\s+(.+)$",
        r"(?:what\s+time\s+is\s+it|time)\s+(?:now\s+)?in\s+(.+)$",
    )
    for pattern in patterns:
        m = re.search(pattern, src, flags=re.IGNORECASE)
        if m:
            return _collapse_spaces(m.group(1).strip(" ?!."))
    return ""


def fetch_time_answer(query: str, *, timeout: float = 4.5, language: str = "ru") -> str | None:
    location_hint = _extract_time_location_hint(query)
    if not location_hint:
        now = datetime.now().astimezone()
        return f"Сейчас локальное время: {now.strftime('%Y-%m-%d %H:%M')} ({now.tzname() or 'local time'})."

    place = _resolve_place(location_hint, timeout=timeout, language=language)
    if not place:
        return None
    timezone_name = _collapse_spaces(place.get("timezone") or "")
    if not timezone_name or ZoneInfo is None:
        return None
    try:
        now = datetime.now(ZoneInfo(timezone_name))
    except Exception:
        return None
    location_text = _place_label(place, location_hint)
    return f"Сейчас в {location_text} {now.strftime('%Y-%m-%d %H:%M')} ({timezone_name})."


def _extract_currency_codes(query: str) -> list[str]:
    low = " " + _collapse_spaces(query).lower() + " "
    normalized = low.replace("$", " usd ").replace("₽", " rub ")
    found: list[str] = []
    for code, aliases in _CURRENCY_ALIASES.items():
        for alias in aliases:
            pattern = rf"(?<!\w){re.escape(alias)}(?!\w)"
            if re.search(pattern, normalized):
                found.append(code)
                break
    return found


def looks_like_currency_query(query: str) -> bool:
    low = _collapse_spaces(query).lower()
    if not low:
        return False
    if "курс" in low or "валют" in low or "конверт" in low or "convert" in low:
        return True
    codes = _extract_currency_codes(query)
    if len(codes) >= 2:
        return True
    return len(codes) == 1 and bool(re.search(r"\b(в|к|to)\b", low))


def _parse_currency_request(query: str) -> tuple[float, str, str] | None:
    codes = _extract_currency_codes(query)
    if not codes:
        return None
    low = _collapse_spaces(query).lower()
    amount_match = re.search(r"(\d+(?:[.,]\d+)?)", low)
    amount = float(amount_match.group(1).replace(",", ".")) if amount_match else 1.0

    if len(codes) >= 2:
        from_code, to_code = codes[0], codes[1]
    else:
        from_code = codes[0]
        if any(marker in low for marker in ("к руб", "в руб", "рубл", "to rub")) and from_code != "RUB":
            to_code = "RUB"
        elif any(marker in low for marker in ("к доллар", "в доллар", "to usd")) and from_code != "USD":
            to_code = "USD"
        elif any(marker in low for marker in ("к евро", "в евро", "to eur")) and from_code != "EUR":
            to_code = "EUR"
        elif from_code == "RUB":
            to_code = "USD"
        else:
            to_code = "RUB"

    if from_code == to_code:
        return None
    return amount, from_code, to_code


def _format_rate_value(value: float) -> str:
    if abs(value - round(value)) < 1e-9:
        return str(int(round(value)))
    if value >= 100:
        return f"{value:.2f}"
    if value >= 1:
        return f"{value:.4f}".rstrip("0").rstrip(".")
    return f"{value:.6f}".rstrip("0").rstrip(".")


def fetch_exchange_rate_answer(query: str, *, timeout: float = 5.0) -> str | None:
    parsed = _parse_currency_request(query)
    if not parsed:
        return None
    amount, from_code, to_code = parsed
    try:
        resp = requests.get(
            f"https://open.er-api.com/v6/latest/{from_code}",
            headers=_HEADERS,
            timeout=max(2.0, timeout),
        )
        resp.raise_for_status()
        data = resp.json()
    except Exception:
        return None
    if str(data.get("result") or "").lower() != "success":
        return None
    rates = dict(data.get("rates") or {})
    rate_value = rates.get(to_code)
    if rate_value is None:
        return None
    try:
        rate = float(rate_value)
    except Exception:
        return None
    updated = _collapse_spaces(data.get("time_last_update_utc") or data.get("time_next_update_utc") or "")
    converted = amount * rate
    if abs(amount - 1.0) < 1e-9:
        return f"По курсу на {updated}, 1 {from_code} ≈ {_format_rate_value(rate)} {to_code}."
    return (
        f"По курсу на {updated}, 1 {from_code} ≈ {_format_rate_value(rate)} {to_code}. "
        f"{_format_rate_value(amount)} {from_code} ≈ {_format_rate_value(converted)} {to_code}."
    )


def looks_like_reference_query(query: str) -> bool:
    low = _collapse_spaces(query).lower()
    if not low:
        return False
    personal_markers = (
        "как меня зовут",
        "кто я",
        "обо мне",
        "в проект",
        "проект",
        "погод",
        "новост",
        "время",
        "который час",
        "сколько времени",
        "курс ",
        "валют",
    )
    if any(marker in low for marker in personal_markers):
        return False
    if any(marker in low for marker in _REFERENCE_QUERY_MARKERS):
        return True
    if re.match(r"^(кто|что|где|когда)\b", low):
        return True
    return False


def _wiki_api_json(lang: str, params: dict[str, Any], *, timeout: float) -> dict[str, Any] | None:
    try:
        resp = requests.get(
            f"https://{lang}.wikipedia.org/w/api.php",
            params=params,
            headers=_HEADERS,
            timeout=max(2.0, timeout),
        )
        resp.raise_for_status()
        return resp.json()
    except Exception:
        return None


def _wiki_search_titles(query: str, lang: str, *, timeout: float) -> list[str]:
    data = _wiki_api_json(
        lang,
        {
            "action": "query",
            "list": "search",
            "srsearch": query,
            "format": "json",
            "utf8": 1,
            "srlimit": 5,
        },
        timeout=timeout,
    )
    if not data:
        return []
    titles: list[str] = []
    for item in list(data.get("query", {}).get("search", []) or []):
        title = _collapse_spaces(item.get("title") or "")
        if title:
            titles.append(title)
    return _unique_keep_order(titles)


def _wiki_summary_extract(title: str, lang: str, *, timeout: float) -> str:
    clean_title = _collapse_spaces(title)
    if not clean_title:
        return ""
    raw = _http_get_text_cached(
        f"https://{lang}.wikipedia.org/api/rest_v1/page/summary/{quote(clean_title.replace(' ', '_'))}",
        timeout=timeout,
    )
    if not raw:
        return ""
    try:
        data = json.loads(raw)
    except Exception:
        return ""
    return _collapse_spaces(data.get("extract") or "")


def _wiki_search_extracts(query: str, lang: str, *, timeout: float) -> list[tuple[str, str]]:
    search_timeout = max(1.1, min(timeout, 2.0))
    summary_timeout = max(1.0, min(timeout, 2.0))
    out: list[tuple[str, str]] = []
    for title in _wiki_search_titles(query, lang, timeout=search_timeout)[:3]:
        extract = _wiki_summary_extract(title, lang, timeout=summary_timeout)
        if extract:
            out.append((title, extract))
    return out


def _wiki_extract(title: str, lang: str, *, timeout: float) -> str:
    data = _wiki_api_json(
        lang,
        {
            "action": "query",
            "prop": "extracts",
            "exintro": 1,
            "explaintext": 1,
            "redirects": 1,
            "titles": title,
            "format": "json",
            "utf8": 1,
        },
        timeout=timeout,
    )
    if not data:
        return ""
    pages = dict(data.get("query", {}).get("pages", {}) or {})
    for page in pages.values():
        text = _collapse_spaces(page.get("extract") or "")
        if text:
            return text
    return ""


def _is_capital_reference_query(query: str) -> bool:
    low = _collapse_spaces(query).lower()
    return "столиц" in low or "capital" in low


def _reference_subject_hint(query: str) -> str:
    src = _collapse_spaces(query).strip(" ?!.,;:")
    if not src:
        return ""
    patterns = (
        r"^(?:что такое|кто такой|кто такая|кто такие|где находится|где расположен|где расположена|когда родился|когда родилась|когда появился|когда появилась)\s+(.+)$",
        r"^(?:what is|who is|where is|when was)\s+(.+)$",
    )
    for pattern in patterns:
        m = re.match(pattern, src, flags=re.IGNORECASE)
        if m:
            return _collapse_spaces(m.group(1))
    return ""


def _reference_display_title(title: str) -> str:
    clean = _collapse_spaces(title)
    if not clean:
        return ""
    if clean[:1].islower():
        return clean[:1].upper() + clean[1:]
    return clean


def _best_reference_excerpt(query: str, title: str, extract: str) -> str:
    text = _collapse_spaces(extract)
    if not text:
        return ""
    text = re.sub(r"\bМФА:\s*[^.?!;—-]{0,48}", "", text, flags=re.IGNORECASE)
    text = re.sub(r"\s*\([^)]{1,40}\)\s*", " ", text)
    text = re.sub(r"\(\s*\)", "", text)
    text = _collapse_spaces(text)
    sentences = [s.strip() for s in re.split(r"(?<=[.!?])\s+", text) if s.strip()]
    if not sentences:
        return ""
    query_low = _collapse_spaces(query).lower()
    preferred: list[str] = []
    if "столиц" in query_low or "capital" in query_low:
        preferred = [s for s in sentences if "столиц" in s.lower() or "capital" in s.lower()]
    elif query_low.startswith("где") or "where is" in query_low:
        preferred = [s for s in sentences if "наход" in s.lower() or "располож" in s.lower() or "located" in s.lower()]
    elif query_low.startswith("когда") or "when" in query_low:
        preferred = [s for s in sentences if re.search(r"\b\d{3,4}\b", s)]

    answer = (preferred[0] if preferred else sentences[0]).strip()
    if title and _contains_cyrillic(title):
        answer = re.sub(
            r"^[A-Za-z][A-Za-z0-9 .,'()/-]{0,40}\s+—\s+",
            f"{title} — ",
            answer,
        )
    answer = re.sub(r"\s*\([^)]{1,40}\)\s*", " ", answer)
    answer = _collapse_spaces(answer)
    answer = re.sub(r"\s+([,.;:!?])", r"\1", answer)
    if title:
        answer_head = answer.split(" — ", 1)[0].split(": ", 1)[0]
        if _normalize_key(_fold_diacritics(answer_head)) == _normalize_key(_fold_diacritics(title)):
            answer = title + answer[len(answer_head):]
    if len(answer) > 220:
        answer = answer[:217].rstrip(" ,;:") + "..."
    folded_answer = _normalize_key(_fold_diacritics(answer))
    folded_title = _normalize_key(_fold_diacritics(title))
    if title and folded_title and folded_title not in folded_answer and not answer.startswith(title):
        answer = f"{title}: {answer}"
    if answer and answer[-1] not in ".!?":
        answer += "."
    return answer


def fetch_reference_answer(query: str, *, timeout: float = 6.0) -> str | None:
    q = _collapse_spaces(query)
    if not q:
        return None
    cache_key = _normalize_key(q)
    now = time.monotonic()
    with _REFERENCE_CACHE_LOCK:
        cached = _REFERENCE_CACHE.get(cache_key)
        if cached and (now - cached[0]) <= _REFERENCE_CACHE_TTL_SECONDS:
            return cached[1]

    languages = ["ru", "en"] if _contains_cyrillic(q) else ["en", "ru"]
    fast_timeout = max(1.2, min(timeout, 2.2))
    slow_timeout = max(1.5, min(timeout, 2.8))
    subject_hint = _reference_subject_hint(q)
    use_direct_subject = bool(subject_hint) and not _is_capital_reference_query(q)
    for lang in languages:
        if use_direct_subject:
            direct_extract = _wiki_summary_extract(subject_hint, lang, timeout=fast_timeout)
            answer = _best_reference_excerpt(q, _reference_display_title(subject_hint), direct_extract)
            if answer:
                with _REFERENCE_CACHE_LOCK:
                    _REFERENCE_CACHE[cache_key] = (now, answer)
                    if len(_REFERENCE_CACHE) > 512:
                        stale = sorted(_REFERENCE_CACHE.items(), key=lambda item: item[1][0])[:128]
                        for stale_key, _ in stale:
                            _REFERENCE_CACHE.pop(stale_key, None)
                return answer
        search_query = q if _is_capital_reference_query(q) else (subject_hint or q)
        search_titles = _wiki_search_titles(search_query, lang, timeout=fast_timeout)
        for title in search_titles[:3]:
            extract = _wiki_summary_extract(title, lang, timeout=fast_timeout)
            answer = _best_reference_excerpt(q, title, extract)
            if answer:
                with _REFERENCE_CACHE_LOCK:
                    _REFERENCE_CACHE[cache_key] = (now, answer)
                    if len(_REFERENCE_CACHE) > 512:
                        stale = sorted(_REFERENCE_CACHE.items(), key=lambda item: item[1][0])[:128]
                        for stale_key, _ in stale:
                            _REFERENCE_CACHE.pop(stale_key, None)
                return answer
        titles = _wiki_search_titles(search_query, lang, timeout=slow_timeout)
        for title in titles[:2]:
            extract = _wiki_summary_extract(title, lang, timeout=slow_timeout) or _wiki_extract(title, lang, timeout=slow_timeout)
            answer = _best_reference_excerpt(q, title, extract)
            if answer:
                with _REFERENCE_CACHE_LOCK:
                    _REFERENCE_CACHE[cache_key] = (now, answer)
                    if len(_REFERENCE_CACHE) > 512:
                        stale = sorted(_REFERENCE_CACHE.items(), key=lambda item: item[1][0])[:128]
                        for stale_key, _ in stale:
                            _REFERENCE_CACHE.pop(stale_key, None)
                return answer
    return None
