from __future__ import annotations

from dataclasses import dataclass


@dataclass
class SeoPackage:
    article_md: str
    faq_json: dict
    schema_json: dict
    internal_links_json: dict


def generate_seo_article(title: str, hook: str, angle: str, cta: str) -> SeoPackage:
    article_md = (
        f"# {title}\n\n"
        f"## Введение\n{hook}\n\n"
        f"## Решение\n{angle}\n\n"
        "## Частые вопросы\n- Вопрос 1?\n- Вопрос 2?\n\n"
        f"**CTA:** {cta}\n"
    )
    faq_json = {
        "questions": [
            {"q": "Что это?", "a": "Короткое объяснение."},
            {"q": "Как применять?", "a": "Пошаговая инструкция."},
        ]
    }
    schema_json = {
        "@context": "https://schema.org",
        "@type": "Article",
        "headline": title,
    }
    internal_links_json = {
        "links": [
            {"title": "База знаний", "url": "/knowledge"},
            {"title": "Руководство", "url": "/guide"},
        ]
    }
    return SeoPackage(
        article_md=article_md,
        faq_json=faq_json,
        schema_json=schema_json,
        internal_links_json=internal_links_json,
    )
