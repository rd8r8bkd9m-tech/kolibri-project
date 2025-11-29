"""Simple tokenization utilities for text processing."""

from __future__ import annotations

from typing import List


def simple_tokenize(text: str, vocab_size: int = 32000, max_length: int = 512) -> List[int]:
    """Simple character-based tokenization.

    Converts characters to token IDs suitable for model input.

    Args:
        text: Input text to tokenize.
        vocab_size: Size of vocabulary (IDs wrapped to this range).
        max_length: Maximum sequence length.

    Returns:
        List of token IDs.
    """
    tokens: List[int] = []
    for char in text.lower():
        if char.isalnum():
            tokens.append(ord(char) % vocab_size)
        elif char == " ":
            tokens.append(0)
    return tokens[:max_length] if tokens else [0]


def detokenize(token_ids: List[int]) -> str:
    """Convert token IDs back to text (approximate).

    Args:
        token_ids: List of token IDs.

    Returns:
        Approximate text representation.
    """
    chars = []
    for tid in token_ids:
        if tid == 0:
            chars.append(" ")
        elif 0 < tid < 128:
            chars.append(chr(tid))
        else:
            chars.append(chr(ord("a") + (tid % 26)))
    return "".join(chars)
