import httpx
from pathlib import Path
import re

urls = [
    ("Kolibri_OS", "https://ru.wikipedia.org/wiki/%D0%9A%D0%BE%D0%BB%D0%B8%D0%B1%D1%80%D0%B8_(%D0%BE%D0%BF%D0%B5%D1%80%D0%B0%D1%86%D0%B8%D0%BE%D0%BD%D0%BD%D0%B0%D1%8F_%D1%81%D0%B8%D1%81%D1%82%D0%B5%D0%BC%D0%B0)"),
    ("AI", "https://ru.wikipedia.org/wiki/%D0%98%D1%81%D0%BA%D1%83%D1%81%D1%81%D1%82%D0%B2%D0%B5%D0%BD%D0%BD%D1%8B%D0%B9_%D0%B8%D0%BD%D1%82%D0%B5%D0%BB%D0%BB%D0%B5%D0%BA%D1%82"),
    ("Evolution", "https://ru.wikipedia.org/wiki/%D0%AD%D0%B2%D0%BE%D0%BB%D1%8E%D1%86%D0%B8%D0%BE%D0%BD%D0%BD%D1%8B%D0%B9_%D0%B0%D0%BB%D0%B3%D0%BE%D1%80%D0%B8%D1%82%D0%BC")
]

output_dir = Path("docs/wikipedia")
output_dir.mkdir(parents=True, exist_ok=True)

headers = {"User-Agent": "KolibriManualFetcher/1.0"}

def clean_html(html):
    # Very basic tag removal
    clean = re.sub(r'<script.*?>.*?</script>', '', html, flags=re.DOTALL)
    clean = re.sub(r'<style.*?>.*?</style>', '', clean, flags=re.DOTALL)
    clean = re.sub(r'<.*?>', '', clean)
    clean = re.sub(r'\n\s*\n', '\n\n', clean)
    return clean

for title, url in urls:
    print(f"Fetching {title}...")
    try:
        resp = httpx.get(url, headers=headers, follow_redirects=True)
        resp.raise_for_status()
        text = clean_html(resp.text)
        (output_dir / f"{title}.md").write_text(f"# {title}\n\n{text}")
        print(f"Saved {title}.md")
    except Exception as e:
        print(f"Failed {title}: {e}")
