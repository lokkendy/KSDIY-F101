#!/usr/bin/env python3
"""Build one F101S3 teaching app using a matching KSDIY SDK in WSL."""
import argparse
from hashlib import sha256
import importlib.util
import json
from pathlib import Path
import re
import shutil
import sys


def main():
    cli = argparse.ArgumentParser(description="在匹配的 KSDIY F101S3 SDK 中编译并打包电子木鱼")
    cli.add_argument("--sdk", type=Path, required=True, help="Linux 文件系统中的 SDK 根目录")
    cli.add_argument("--config", type=Path, required=True, help="随板提供的已验收 wooden_fish.defconfig")
    cli.add_argument("--out", type=Path, required=True, help="尚不存在的输出目录")
    args = cli.parse_args()
    sdk = args.sdk.resolve()
    out = args.out.resolve()
    project = sdk / "lichee/rtos/projects/f101s3/ksdiy_f101s3"
    rtconfig = sdk / "lichee/rtos/.config"
    board_cfg = sdk / "board/f101s3/ksdiy_f101s3/configs/sys_config.fex"
    target = project / "defconfig"
    helper = project / "tools/build_all_apps.py"
    try:
        release_config = args.config.read_bytes()
        if len(re.findall(rb"^CONFIG_APP_(?!UI_SELFTEST)([A-Z_0-9]+)=y$", release_config, re.M)) != 1:
            raise ValueError("配置必须只启用一个应用")
        required = (
            b"CONFIG_APP_WOODEN_FISH=y",
            b"CONFIG_LCD_SUPPORT_MIPI_ST7102_480X800=y",
            b"CONFIG_ARCH_MEM_LENGTH=0xff0000",
        )
        if not set(required).issubset(release_config.splitlines()):
            raise ValueError("配置的应用、屏幕或内存与本课不匹配")
        if re.search(rb"^[ \t]*wifi_sta_(ssid|passwd)[ \t]*=", board_cfg.read_bytes(), re.M):
            raise ValueError("SDK 板级配置含预置 Wi-Fi 字段，请使用随板版干净配置")
        old_target = target.read_bytes()
        old_runtime = rtconfig.read_bytes()
        spec = importlib.util.spec_from_file_location("f101_builder", helper)
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        builder.select_board("s3")
        if builder.SDK.resolve() != sdk:
            raise ValueError("SDK 根目录与工程实际路径不一致")
        out.mkdir(parents=True, exist_ok=False)
        try:
            target.write_bytes(release_config)
            if builder.run_sdk("mrtos", out / "wooden_fish.build.log"):
                raise RuntimeError("编译失败，请查看 wooden_fish.build.log")
            builder.verify_config("wooden_fish")
            binary = builder.verify_binary("page_fish")
            packed = builder.pack_checked(out / "wooden_fish.pack.log", binary)
            image = out / "rtos_f101s3_wooden_fish.img"
            shutil.copyfile(packed, image)
            checksum = sha256(image.read_bytes()).hexdigest()
            if checksum != sha256(packed.read_bytes()).hexdigest():
                raise RuntimeError("导出前后哈希不一致")
            manifest = {
                "app": "wooden_fish", "target": "f101s3_ksdiy_f101s3",
                "status": "ok", "image": image.name, "bytes": image.stat().st_size,
                "sha256": checksum,
            }
            (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
            print(f"已生成 {image}\nSHA-256 {checksum}")
        finally:
            target.write_bytes(old_target)
            rtconfig.write_bytes(old_runtime)
    except (OSError, ValueError, RuntimeError, AssertionError, ImportError) as error:
        print(f"构建未完成：{error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
