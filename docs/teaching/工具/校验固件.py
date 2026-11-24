#!/usr/bin/env python3
"""Check an IMG against the SHA-256 in the teaching kit catalog."""
import argparse
import csv
import hashlib
from pathlib import Path

CATALOG = Path(__file__).resolve().parents[1] / "固件" / "固件索引.csv"


def main():
    parser = argparse.ArgumentParser(description="核对固件索引中的文件名、大小和 SHA-256")
    parser.add_argument("image", type=Path, help="已下载的 IMG 文件")
    parser.add_argument("--catalog", type=Path, default=CATALOG, help="可指定新版索引")
    args = parser.parse_args()
    try:
        with args.catalog.open(encoding="utf-8-sig", newline="") as stream:
            matches = [row for row in csv.DictReader(stream) if row["文件名"] == args.image.name]
        if len(matches) != 1:
            parser.exit(2, "索引中没有唯一匹配的文件名，请核对应用和版本。\n")
        row = matches[0]
        with args.image.open("rb") as stream:
            hasher = hashlib.sha256()
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                hasher.update(block)
            digest = hasher.hexdigest()
        size = args.image.stat().st_size
        if size != int(row["字节数"]) or digest != row["SHA-256"].lower():
            parser.exit(1, f"校验失败\n实际 SHA-256 {digest}\n")
    except (OSError, KeyError, ValueError) as error:
        parser.exit(2, f"无法校验：{error}\n")
    print(f"校验通过 · {row['应用']} {row['版本']} · {size:,} bytes\nSHA-256 {digest}")


if __name__ == "__main__":
    main()
