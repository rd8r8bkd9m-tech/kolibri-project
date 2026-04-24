import re
import time
from pathlib import Path

import httpx

# Letters to fetch for a complete set
LETTERS = [
    "А",
    "Б",
    "В",
    "Г",
    "Д",
    "Е",
    "Ж",
    "З",
    "И",
    "Й",
    "К",
    "Л",
    "М",
    "Н",
    "О",
    "П",
    "Р",
    "С",
    "Т",
    "У",
    "Ф",
    "Х",
    "Ц",
    "Ч",
    "Ш",
    "Щ",
    "Э",
    "Ю",
    "Я",
]
BASE_URL = "https://ru.wikisource.org/wiki/%D0%A2%D0%A1%D0%96%D0%92%D0%AF/"

output_dir = Path("docs/dal")
output_dir.mkdir(parents=True, exist_ok=True)

headers = {"User-Agent": "KolibriDalFetcher/1.0"}


def clean_wikisource(html):
    # Remove scripts, styles, and navigation
    clean = re.sub(r"<script.*?>.*?</script>", "", html, flags=re.DOTALL)
    clean = re.sub(r"<style.*?>.*?</style>", "", clean, flags=re.DOTALL)
    # Extract only the main content if possible (Wikisource uses mw-parser-output)
    match = re.search(
        r'<div class="mw-parser-output">(.*?)</div>', clean, flags=re.DOTALL
    )
    if match:
        clean = match.group(1)

    # Remove table of contents
    clean = re.sub(r'<div id="toc".*?>.*?</div>', "", clean, flags=re.DOTALL)

    # Strip remaining tags
    clean = re.sub(r"<.*?>", "", clean)
    # Unescape HTML entities
    import html as html_lib

    clean = html_lib.unescape(clean)

    # Clean up whitespace
    clean = re.sub(r"\n\s*\n", "\n\n", clean)
    return clean.strip()


for letter in LETTERS:
    url = BASE_URL + letter
    print(f"Fetching Letter {letter} from Wikisource...")
    try:
        resp = httpx.get(url, headers=headers, follow_redirects=True, timeout=30)
        resp.raise_for_status()
        text = clean_wikisource(resp.text)

        # Split into smaller chunks if it's too big, but for now we save as one doc
        (output_dir / f"Dal_{letter}.md").write_text(
            f"# Толковый словарь Даля - Буква {letter}\n\n{text}"
        )
        print(f"Saved Dal_{letter}.md")
        time.sleep(1)  # Be nice to Wikisource
    except Exception as e:
        print(f"Failed {letter}: {e}")

print("Fetch complete.")
