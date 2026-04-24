import os
import re


def split_dal(input_path, output_dir):
    if not os.path.exists(output_dir):
        os.makedirs(output_dir)

    with open(input_path, "r", encoding="utf-8") as f:
        content = f.read()

    # Ищем строки вида **СЛОВО**, ...
    entries = re.findall(
        r"\*\*([^*]+)\*\*,\s*(.*?)(?=\n\*\*|\uffff|$)", content, re.DOTALL
    )

    for i, (word, definition) in enumerate(entries):
        word = word.strip()
        filename = f"dal_{word.replace(' ', '_')}.md"
        with open(os.path.join(output_dir, filename), "w", encoding="utf-8") as out:
            out.write(f"# {word}\n\n{definition.strip()}")

    print(f"Split Dal dictionary into {len(entries)} entries in {output_dir}")


if __name__ == "__main__":
    split_dal("docs/dal/dal_volume_1.md", "data/dal")
