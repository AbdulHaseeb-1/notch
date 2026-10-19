#!/usr/bin/env python3
"""Writes resources/moonshine/vocab.json (token id -> piece) from Moonshine's tokenizer.json.

The C++ decoder only needs the pieces: it maps ids back to text (a SentencePiece
style vocabulary: U+2581 is a space, <0xNN> is a raw byte), so the 3.7 MB
tokenizer.json with its merge table is not shipped.

    python tools/extract_moonshine_vocab.py path/to/tokenizer.json resources/moonshine/vocab.json
"""
import json
import sys

src, dst = sys.argv[1], sys.argv[2]
tok = json.load(open(src, encoding="utf-8"))
size = 1 + max(max(tok["model"]["vocab"].values()), max(a["id"] for a in tok["added_tokens"]))
pieces = [""] * size
for piece, i in tok["model"]["vocab"].items():
    pieces[i] = piece
for a in tok["added_tokens"]:
    pieces[a["id"]] = a["content"]
json.dump(pieces, open(dst, "w", encoding="utf-8"), ensure_ascii=False, separators=(",", ":"))
print(f"{len(pieces)} tokens -> {dst}")
