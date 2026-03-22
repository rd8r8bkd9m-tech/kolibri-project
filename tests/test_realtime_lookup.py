from __future__ import annotations

from datetime import datetime
import pytest


class _FakeResponse:
    def __init__(self, payload=None, text: str = "", status_code: int = 200):
        self._payload = payload if payload is not None else {}
        self.text = text
        self.status_code = status_code

    def raise_for_status(self) -> None:
        if self.status_code >= 400:
            raise RuntimeError(f"HTTP {self.status_code}")

    def json(self):
        return self._payload


@pytest.fixture(autouse=True)
def _clear_realtime_http_cache():
    from backend.service import realtime_lookup

    realtime_lookup._RSS_HTTP_CACHE.clear()
    realtime_lookup._NEWS_DIGEST_CACHE.clear()
    realtime_lookup._REFERENCE_CACHE.clear()
    yield
    realtime_lookup._RSS_HTTP_CACHE.clear()
    realtime_lookup._NEWS_DIGEST_CACHE.clear()
    realtime_lookup._REFERENCE_CACHE.clear()


def test_fetch_weather_answer_normalizes_russian_city_case(monkeypatch):
    from backend.service.realtime_lookup import fetch_weather_answer

    geocode_names: list[str] = []

    def fake_get(url, params=None, headers=None, timeout=None):
        if "geocoding-api.open-meteo.com" in url:
            name = str((params or {}).get("name") or "")
            geocode_names.append(name)
            if name == "лениногорске":
                return _FakeResponse({"results": []})
            if name == "лениногорск":
                return _FakeResponse(
                    {
                        "results": [
                            {
                                "name": "Лениногорск",
                                "latitude": 54.60256,
                                "longitude": 52.46087,
                                "country_code": "RU",
                                "country": "Россия",
                                "admin1": "Татарстан",
                                "population": 66263,
                            }
                        ]
                    }
                )
            raise AssertionError(f"Unexpected geocode variant: {name}")
        if "api.open-meteo.com" in url:
            return _FakeResponse(
                {
                    "current": {
                        "temperature_2m": -6.7,
                        "apparent_temperature": -11.5,
                        "precipitation": 0.0,
                        "weather_code": 77,
                        "wind_speed_10m": 11.6,
                    },
                    "daily": {
                        "weather_code": [77],
                        "temperature_2m_max": [-5.0],
                        "temperature_2m_min": [-12.0],
                        "precipitation_probability_max": [20],
                    },
                }
            )
        raise AssertionError(f"Unexpected URL: {url}")

    monkeypatch.setattr("backend.service.realtime_lookup.requests.get", fake_get)

    answer = fetch_weather_answer("лениногорске")

    assert answer is not None
    assert "Лениногорск" in answer
    assert "ощущается как -11.5" in answer
    assert geocode_names[:2] == ["лениногорске", "лениногорск"]


def test_fetch_news_digest_parses_bing_rss(monkeypatch):
    from backend.service.realtime_lookup import fetch_news_digest

    xml = """<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0">
  <channel>
    <title>test</title>
    <item>
      <title>Первый заголовок</title>
      <description>Короткое описание первой новости.</description>
      <link>https://example.com/1</link>
    </item>
    <item>
      <title>Второй заголовок</title>
      <description>Описание второй новости.</description>
      <link>https://another.example.org/2</link>
    </item>
  </channel>
</rss>
"""

    def fake_get(url, params=None, headers=None, timeout=None):
        return _FakeResponse(text=xml)

    monkeypatch.setattr("backend.service.realtime_lookup.requests.get", fake_get)
    monkeypatch.setattr("backend.service.realtime_lookup.fetch_curated_world_news_items", lambda **kwargs: [])

    answer = fetch_news_digest("мировые новости сегодня", timeout=3.0, max_items=2)

    assert answer is not None
    assert "мировые новости сегодня" in answer
    assert "Первый заголовок" in answer
    assert "Второй заголовок" in answer


def test_fetch_news_digest_diversifies_domains(monkeypatch):
    from backend.service.realtime_lookup import fetch_news_digest

    def fake_fetch_bing_search_rss(query: str, *, timeout: float = 5.0, max_items: int = 5):
        if "мировые новости сегодня" in query:
            return [
                {"title": "Новость A", "description": "Описание A", "link": "https://ria.ru/a", "domain": "ria.ru"},
                {"title": "Новость B", "description": "Описание B", "link": "https://ria.ru/b", "domain": "ria.ru"},
            ]
        if "ключевые события дня в мире" in query:
            return [
                {
                    "title": "Саммит лидеров региона согласовал дорожную карту",
                    "description": "Ключевые международные события дня.",
                    "link": "https://tass.ru/c",
                    "domain": "tass.ru",
                    "published_at": "Thu, 05 Mar 2026 19:00:00 GMT",
                },
            ]
        return [
            {"title": "Новость D", "description": "Описание D", "link": "https://bbc.com/d", "domain": "bbc.com"},
        ]

    monkeypatch.setattr("backend.service.realtime_lookup.fetch_bing_search_rss", fake_fetch_bing_search_rss)
    monkeypatch.setattr("backend.service.realtime_lookup.fetch_curated_world_news_items", lambda **kwargs: [])

    answer = fetch_news_digest("мировые новости сегодня", timeout=3.0, max_items=3)

    assert answer is not None
    assert "ria.ru" in answer
    assert "tass.ru" in answer or "bbc.com" in answer
    assert answer.count("ria.ru") == 1


def test_fetch_news_digest_prefers_fresher_and_more_specific_item(monkeypatch):
    from backend.service.realtime_lookup import fetch_news_digest

    def fake_fetch_bing_search_rss(query: str, *, timeout: float = 5.0, max_items: int = 5):
        return [
            {
                "title": "Новости мира сегодня",
                "description": "Свежие новости мира.",
                "link": "https://example.com/news",
                "domain": "example.com",
                "source": "Example News",
                "published_at": "Thu, 05 Mar 2026 08:00:00 GMT",
            },
            {
                "title": "Саммит лидеров ЕС согласовал новый пакет санкций",
                "description": "Решение принято после экстренных переговоров в Брюсселе.",
                "link": "https://reuters.com/world/europe/summit-package",
                "domain": "reuters.com",
                "source": "Reuters",
                "published_at": "Fri, 06 Mar 2026 20:00:00 GMT",
            },
        ]

    monkeypatch.setattr("backend.service.realtime_lookup.fetch_bing_search_rss", fake_fetch_bing_search_rss)
    monkeypatch.setattr("backend.service.realtime_lookup.fetch_curated_world_news_items", lambda **kwargs: [])

    answer = fetch_news_digest("мировые новости сегодня", timeout=3.0, max_items=1)

    assert answer is not None
    assert "Саммит лидеров ЕС" in answer
    assert "Reuters" in answer


def test_fetch_news_digest_generic_query_prefers_event_digest_over_lifestyle_noise(monkeypatch):
    from backend.service.realtime_lookup import fetch_news_digest

    def fake_fetch_bing_search_rss(query: str, *, timeout: float = 5.0, max_items: int = 5):
        if query == "какие новости в мире":
            return [
                {
                    "title": "Более 100 туристов на одного жителя: названо самое переполненное место в мире",
                    "description": "Туристический рейтинг самых перегруженных направлений.",
                    "link": "https://travel.example.com/overtourism",
                    "domain": "example.com",
                    "source": "Travel Weekly",
                    "published_at": "Thu, 05 Mar 2026 10:00:00 GMT",
                }
            ]
        if query == "ключевые события дня в мире":
            return [
                {
                    "title": "Главные новости к вечеру 5 марта 2026 года",
                    "description": "Что произошло в России и мире — ключевые события дня.",
                    "link": "https://news.example.com/digest",
                    "domain": "example.com",
                    "source": "News Digest",
                    "published_at": "Thu, 05 Mar 2026 18:00:00 GMT",
                }
            ]
        return []

    monkeypatch.setattr("backend.service.realtime_lookup.fetch_bing_search_rss", fake_fetch_bing_search_rss)
    monkeypatch.setattr("backend.service.realtime_lookup.fetch_curated_world_news_items", lambda **kwargs: [])

    answer = fetch_news_digest("какие новости в мире", timeout=3.0, max_items=1)

    assert answer is not None
    assert "Главные новости к вечеру 5 марта 2026 года" in answer
    assert "туристов" not in answer.lower()


def test_fetch_news_digest_generic_query_drops_low_quality_tail(monkeypatch):
    from backend.service.realtime_lookup import fetch_news_digest

    def fake_fetch_bing_search_rss(query: str, *, timeout: float = 5.0, max_items: int = 5):
        if query == "мировые новости сегодня":
            return [
                {
                    "title": "Главные новости к вечеру 5 марта 2026 года",
                    "description": "Что произошло в России и мире — ключевые события дня.",
                    "link": "https://news.example.com/digest-1",
                    "domain": "digest.example.com",
                    "source": "News Digest",
                    "published_at": "Thu, 05 Mar 2026 18:00:00 GMT",
                },
                {
                    "title": "Удары США и Израиля по Ирану: главные события в ночь на 5 марта",
                    "description": "Ключевые международные события и последствия переговоров.",
                    "link": "https://events.example.com/iran",
                    "domain": "events.example.com",
                    "source": "Events Wire",
                    "published_at": "Thu, 05 Mar 2026 15:00:00 GMT",
                },
                {
                    "title": "Какой сегодня день? Праздники и главные события 12 декабря 2025 года",
                    "description": "Узнайте, какие праздники и традиции отмечают сегодня.",
                    "link": "https://lifestyle.example.com/day",
                    "domain": "lifestyle.example.com",
                    "source": "Lifestyle Daily",
                    "published_at": "Tue, 12 Dec 2025 09:00:00 GMT",
                },
            ]
        return []

    monkeypatch.setattr("backend.service.realtime_lookup.fetch_bing_search_rss", fake_fetch_bing_search_rss)
    monkeypatch.setattr("backend.service.realtime_lookup.fetch_curated_world_news_items", lambda **kwargs: [])

    answer = fetch_news_digest("мировые новости сегодня", timeout=3.0, max_items=3)

    assert answer is not None
    assert "Главные новости к вечеру 5 марта 2026 года" in answer
    assert "Удары США и Израиля по Ирану" in answer
    assert "Какой сегодня день?" not in answer


def test_fetch_news_digest_generic_query_penalizes_aggregators(monkeypatch):
    from backend.service.realtime_lookup import fetch_news_digest

    def fake_fetch_bing_search_rss(query: str, *, timeout: float = 5.0, max_items: int = 5):
        if query == "мировые новости сегодня":
            return [
                {
                    "title": "Главные новости к вечеру 5 марта 2026 года",
                    "description": "Что произошло в России и мире — ключевые события дня.",
                    "link": "https://news.mail.ru/politics/1/",
                    "domain": "news.mail.ru",
                    "source": "Новости Mail.ru",
                    "published_at": "Thu, 05 Mar 2026 18:00:00 GMT",
                },
                {
                    "title": "Саммит лидеров ЕС согласовал новый пакет санкций",
                    "description": "Решение принято после экстренных переговоров в Брюсселе.",
                    "link": "https://reuters.com/world/europe/summit-package",
                    "domain": "reuters.com",
                    "source": "Reuters",
                    "published_at": "Thu, 05 Mar 2026 17:00:00 GMT",
                },
            ]
        return []

    monkeypatch.setattr("backend.service.realtime_lookup.fetch_bing_search_rss", fake_fetch_bing_search_rss)
    monkeypatch.setattr("backend.service.realtime_lookup.fetch_curated_world_news_items", lambda **kwargs: [])

    answer = fetch_news_digest("мировые новости сегодня", timeout=3.0, max_items=1)

    assert answer is not None
    assert "Саммит лидеров ЕС" in answer
    assert "Mail.ru" not in answer


def test_fetch_news_digest_generic_query_avoids_near_duplicate_third_item(monkeypatch):
    from backend.service.realtime_lookup import fetch_news_digest

    def fake_curated_world_news_items(*, timeout: float = 5.0, max_items: int = 3):
        return [
            {
                "title": "Итальянский министр заявил о начале операции против Ирана 31 февраля",
                "description": "",
                "link": "https://lenta.example.com/a",
                "domain": "lenta.example.com",
                "source": "Lenta.ru Мир",
                "published_at": "Thu, 05 Mar 2026 18:00:00 GMT",
                "score": "138.00",
            },
            {
                "title": "Израиль начал волну авиаударов по инфраструктуре в Тегеране и Исфахане",
                "description": "",
                "link": "https://ria.example.com/b",
                "domain": "ria.example.com",
                "source": "РИА Новости",
                "published_at": "Thu, 05 Mar 2026 17:30:00 GMT",
                "score": "137.00",
            },
            {
                "title": "Глава МИД Италии оговорился про начало операции против Ирана «31 февраля»",
                "description": "",
                "link": "https://rbc.example.com/c",
                "domain": "rbc.example.com",
                "source": "РБК Мир",
                "published_at": "Thu, 05 Mar 2026 17:00:00 GMT",
                "score": "135.00",
            },
            {
                "title": "Главные события в ООН и мире за неделю",
                "description": "",
                "link": "https://un.example.com/d",
                "domain": "un.example.com",
                "source": "UN News",
                "published_at": "Thu, 05 Mar 2026 16:00:00 GMT",
                "score": "132.00",
            },
        ]

    monkeypatch.setattr("backend.service.realtime_lookup.fetch_curated_world_news_items", fake_curated_world_news_items)
    monkeypatch.setattr("backend.service.realtime_lookup.fetch_bing_search_rss", lambda *args, **kwargs: [])

    answer = fetch_news_digest("какие новости в мире", timeout=3.0, max_items=3)

    assert answer is not None
    assert "Итальянский министр заявил" in answer
    assert "Израиль начал волну авиаударов" in answer
    assert "Главные события в ООН и мире за неделю" in answer
    assert "Глава МИД Италии оговорился" not in answer


def test_fetch_news_digest_generic_query_uses_curated_sources_and_compact_format(monkeypatch):
    from backend.service.realtime_lookup import fetch_news_digest

    def fake_fetch_bing_search_rss(query: str, *, timeout: float = 5.0, max_items: int = 5):
        return [
            {
                "title": "Главные новости к вечеру 5 марта 2026 года",
                "description": "Что произошло в России и мире — ключевые события дня. Последние новости общества, экономики и политики.",
                "link": "https://mail.example.com/digest",
                "domain": "mail.example.com",
                "source": "Новости Mail.ru",
                "published_at": "Thu, 05 Mar 2026 18:00:00 GMT",
            }
        ]

    def fake_curated_world_news_items(*, timeout: float = 5.0, max_items: int = 3):
        return [
            {
                "title": "Война США и Израиля с Ираном: Тегеран атаковало более 80 истребителей",
                "description": "Короткое описание, которое не должно дублироваться в компактном ответе.",
                "link": "https://bbc.example.com/world",
                "domain": "bbc.example.com",
                "source": "BBC Русская служба",
                "published_at": "Thu, 05 Mar 2026 19:00:00 GMT",
                "score": "124.00",
            },
            {
                "title": "Главные новости дня | пятница: Ближний Восток, женщины, цены на продовольствие",
                "description": "Мировые цены на продовольствие выросли впервые за пять месяцев. Дополнительная строка, которую надо сжать.",
                "link": "https://un.example.com/world",
                "domain": "un.example.com",
                "source": "UN News",
                "published_at": "Thu, 05 Mar 2026 17:00:00 GMT",
                "score": "118.00",
            },
        ]

    monkeypatch.setattr("backend.service.realtime_lookup.fetch_bing_search_rss", fake_fetch_bing_search_rss)
    monkeypatch.setattr("backend.service.realtime_lookup.fetch_curated_world_news_items", fake_curated_world_news_items)

    answer = fetch_news_digest("мировые новости сегодня", timeout=3.0, max_items=3)

    assert answer is not None
    assert answer.startswith("Сейчас по теме")
    assert "BBC Русская служба" in answer
    assert "UN News" in answer
    assert "Последние новости общества, экономики и политики" not in answer
    assert "Короткое описание, которое не должно дублироваться" not in answer


def test_fetch_news_digest_generic_query_skips_bing_when_curated_enough(monkeypatch):
    from backend.service.realtime_lookup import fetch_news_digest

    called = {"bing": 0}

    def fake_fetch_bing_search_rss(query: str, *, timeout: float = 5.0, max_items: int = 5):
        called["bing"] += 1
        return []

    def fake_curated_world_news_items(*, timeout: float = 5.0, max_items: int = 3):
        return [
            {
                "title": "Новость A",
                "description": "Описание A",
                "link": "https://bbc.example.com/a",
                "domain": "bbc.example.com",
                "source": "BBC Русская служба",
                "published_at": "Thu, 05 Mar 2026 18:00:00 GMT",
                "score": "125.00",
            },
            {
                "title": "Новость B",
                "description": "Описание B",
                "link": "https://meduza.example.com/b",
                "domain": "meduza.example.com",
                "source": "Медуза",
                "published_at": "Thu, 05 Mar 2026 17:30:00 GMT",
                "score": "122.00",
            },
            {
                "title": "Новость C",
                "description": "Описание C",
                "link": "https://rbc.example.com/c",
                "domain": "rbc.example.com",
                "source": "РБК Мир",
                "published_at": "Thu, 05 Mar 2026 17:00:00 GMT",
                "score": "120.00",
            },
        ]

    monkeypatch.setattr("backend.service.realtime_lookup.fetch_bing_search_rss", fake_fetch_bing_search_rss)
    monkeypatch.setattr("backend.service.realtime_lookup.fetch_curated_world_news_items", fake_curated_world_news_items)

    answer = fetch_news_digest("мировые новости сегодня", timeout=3.0, max_items=3)

    assert answer is not None
    assert "Новость A" in answer
    assert "Новость B" in answer
    assert "Новость C" in answer
    assert called["bing"] == 0


def test_fetch_news_digest_uses_ready_answer_cache(monkeypatch):
    from backend.service.realtime_lookup import fetch_news_digest

    called = {"curated": 0}

    def fake_curated_world_news_items(*, timeout: float = 5.0, max_items: int = 3):
        called["curated"] += 1
        return [
            {
                "title": "Новость A",
                "description": "Описание A",
                "link": "https://bbc.example.com/a",
                "domain": "bbc.example.com",
                "source": "BBC Русская служба",
                "published_at": "Thu, 05 Mar 2026 18:00:00 GMT",
                "score": "125.00",
            },
            {
                "title": "Новость B",
                "description": "Описание B",
                "link": "https://meduza.example.com/b",
                "domain": "meduza.example.com",
                "source": "Медуза",
                "published_at": "Thu, 05 Mar 2026 17:30:00 GMT",
                "score": "122.00",
            },
            {
                "title": "Новость C",
                "description": "Описание C",
                "link": "https://rbc.example.com/c",
                "domain": "rbc.example.com",
                "source": "РБК Мир",
                "published_at": "Thu, 05 Mar 2026 17:00:00 GMT",
                "score": "120.00",
            },
        ]

    monkeypatch.setattr("backend.service.realtime_lookup.fetch_curated_world_news_items", fake_curated_world_news_items)
    monkeypatch.setattr("backend.service.realtime_lookup.fetch_bing_search_rss", lambda *args, **kwargs: [])

    first = fetch_news_digest("мировые новости сегодня", timeout=3.0, max_items=3)
    second = fetch_news_digest("мировые новости сегодня", timeout=3.0, max_items=3)

    assert first == second
    assert called["curated"] == 1


def test_fetch_bing_search_rss_skips_wikipedia_titles(monkeypatch):
    from backend.service.realtime_lookup import fetch_bing_search_rss

    xml = """<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0">
  <channel>
    <title>test</title>
    <item>
      <title>Dale Midkiff - Wikipedia</title>
      <description>Actor page.</description>
      <link>https://en.wikipedia.org/wiki/Dale_Midkiff</link>
    </item>
    <item>
      <title>Главная новость дня</title>
      <description>Короткое описание новости.</description>
      <link>https://tass.ru/news/1</link>
    </item>
  </channel>
</rss>
"""

    def fake_get(url, params=None, headers=None, timeout=None):
        return _FakeResponse(text=xml)

    monkeypatch.setattr("backend.service.realtime_lookup.requests.get", fake_get)

    items = fetch_bing_search_rss("world news today", timeout=3.0, max_items=5)

    assert len(items) == 1
    assert items[0]["title"] == "Главная новость дня"


def test_fetch_bing_search_rss_extracts_real_source_and_target(monkeypatch):
    from backend.service.realtime_lookup import fetch_bing_search_rss

    xml = """<?xml version="1.0" encoding="utf-8"?>
<rss version="2.0" xmlns:News="https://example.com/news">
  <channel>
    <title>test</title>
    <item>
      <title>Мировая новость</title>
      <description>Описание новости.</description>
      <link>http://www.bing.com/news/apiclick.aspx?url=https%3A%2F%2Fwww.foxnews.com%2Fworld%2Fstory</link>
      <News:Source>Fox News</News:Source>
    </item>
  </channel>
</rss>
"""

    def fake_get(url, params=None, headers=None, timeout=None):
        return _FakeResponse(text=xml)

    monkeypatch.setattr("backend.service.realtime_lookup.requests.get", fake_get)

    items = fetch_bing_search_rss("world news today", timeout=3.0, max_items=5)

    assert len(items) == 1
    assert items[0]["source"] == "Fox News"
    assert items[0]["domain"] == "foxnews.com"
    assert items[0]["link"] == "https://www.foxnews.com/world/story"


def test_fetch_exchange_rate_answer(monkeypatch):
    from backend.service.realtime_lookup import fetch_exchange_rate_answer

    def fake_get(url, params=None, headers=None, timeout=None):
        return _FakeResponse(
            {
                "result": "success",
                "time_last_update_utc": "Fri, 06 Mar 2026 00:00:01 +0000",
                "rates": {"RUB": 91.25},
            }
        )

    monkeypatch.setattr("backend.service.realtime_lookup.requests.get", fake_get)

    answer = fetch_exchange_rate_answer("100 долларов в рублях")

    assert answer is not None
    assert "1 USD" in answer
    assert "100 USD" in answer
    assert "RUB" in answer


def test_fetch_time_answer_for_city(monkeypatch):
    from backend.service.realtime_lookup import fetch_time_answer

    monkeypatch.setattr(
        "backend.service.realtime_lookup._resolve_place",
        lambda *args, **kwargs: {
            "name": "Париж",
            "admin1": "Иль-де-Франс",
            "country": "Франция",
            "timezone": "Europe/Paris",
        },
    )

    answer = fetch_time_answer("сколько времени в Париже?")

    assert answer is not None
    assert "Париж" in answer
    assert "Europe/Paris" in answer


def test_fetch_reference_answer_from_wikipedia(monkeypatch):
    from backend.service.realtime_lookup import fetch_reference_answer

    def fake_search(query: str, lang: str, *, timeout: float):
        return ["Париж"] if lang == "ru" else []

    def fake_extract(title: str, lang: str, *, timeout: float):
        return "Париж — столица и крупнейший город Франции. Расположен на реке Сена."

    monkeypatch.setattr("backend.service.realtime_lookup._wiki_search_titles", fake_search)
    monkeypatch.setattr("backend.service.realtime_lookup._wiki_extract", fake_extract)

    answer = fetch_reference_answer("какая столица Франции")

    assert answer is not None
    assert "Париж" in answer
    assert "столица" in answer.lower()


def test_fetch_reference_answer_prefers_direct_subject_summary(monkeypatch):
    from backend.service.realtime_lookup import fetch_reference_answer

    called = {"search": 0}

    def fake_summary(title: str, lang: str, *, timeout: float):
        if lang == "ru" and title.lower() == "марс":
            return "Марс — четвёртая по удалённости от Солнца планета Солнечной системы."
        return ""

    def fake_search_extracts(query: str, lang: str, *, timeout: float):
        called["search"] += 1
        return []

    monkeypatch.setattr("backend.service.realtime_lookup._wiki_summary_extract", fake_summary)
    monkeypatch.setattr("backend.service.realtime_lookup._wiki_search_extracts", fake_search_extracts)

    answer = fetch_reference_answer("что такое марс")

    assert answer == "Марс — четвёртая по удалённости от Солнца планета Солнечной системы."
    assert called["search"] == 0


def test_best_reference_excerpt_replaces_latin_alias_with_title():
    from backend.service.realtime_lookup import _best_reference_excerpt

    answer = _best_reference_excerpt(
        "какая столица Франции",
        "Париж",
        "Paris — столица и крупнейший город Франции. Расположен на реке Сена.",
    )

    assert answer == "Париж — столица и крупнейший город Франции."


def test_best_reference_excerpt_is_short_and_human():
    from backend.service.realtime_lookup import _best_reference_excerpt

    answer = _best_reference_excerpt(
        "что такое марс",
        "Марс",
        "Марс (лат. Mars) — четвёртая по удалённости от Солнца планета Солнечной системы. "
        "Названа в честь древнеримского бога войны. Это вторая по величине планета земной группы.",
    )

    assert answer == "Марс — четвёртая по удалённости от Солнца планета Солнечной системы."
