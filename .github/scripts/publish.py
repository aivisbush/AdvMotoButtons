#!/usr/bin/env python3
"""Adds a firmware image (and optionally the Android app) to the Moto Buttons site.

The site is the gh-pages branch, where index.html is edited; this adds
moto-buttons.apk and
  firmware/<board>/<channel>/MotoButtons2-<version>.bin
  firmware/latest.json  {"boards": {"<board>": {"prod": {...}, "beta": {...}}}}
Each entry: {"version", "file", "size", "md5"}. Other boards and channels are kept.
Version and board come from the MBFWVER= / MBBOARD= tags inside the image.

  publish.py --site <gh-pages checkout> --firmware MotoButtons2.ino.bin --channel prod|beta
             [--expect-board esp32c3-oled] [--expect-version 2.4.0]
  publish.py --site <gh-pages checkout> --app app-release.apk
"""
import argparse
import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

MAX_IMAGE = 0x140000


def tag(data, prefix):
    m = re.search(re.escape(prefix).encode() + rb'([0-9A-Za-z.\-]{1,24})', data)
    return m.group(1).decode() if m else None


def publish_firmware(site, image_path, channel, expect_board, expect_version):
    data = Path(image_path).read_bytes()
    if not data or data[0] != 0xE9:
        sys.exit(f'{image_path} is not a controller firmware image')
    if len(data) > MAX_IMAGE:
        sys.exit(f'{image_path} is too big; use the app .bin, not the merged one')
    version, board = tag(data, 'MBFWVER='), tag(data, 'MBBOARD=')
    if not version or not board:
        sys.exit('The image has no MBFWVER=/MBBOARD= tags (needs firmware 2.4.0 or newer)')
    if expect_board and board != expect_board:
        sys.exit(f'The image is for board {board}, expected {expect_board}')
    if expect_version and version != expect_version:
        sys.exit(f'The image is version {version}, but the tag says {expect_version}. Update FIRMWARE_VERSION_TAG in config.h.')

    folder = site / 'firmware' / board / channel
    folder.mkdir(parents=True, exist_ok=True)
    for old in folder.glob('MotoButtons2-*.bin'):
        old.unlink()
    name = f'MotoButtons2-{version}.bin'
    (folder / name).write_bytes(data)

    manifest_path = site / 'firmware' / 'latest.json'
    manifest = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
    boards = manifest.setdefault('boards', {})
    boards.setdefault(board, {})[channel] = {
        'version': version,
        'file': f'{board}/{channel}/{name}',
        'size': len(data),
        'md5': hashlib.md5(data).hexdigest(),
    }
    manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    print(f'Published {board} {channel} {version} ({len(data)} bytes)')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--site', required=True, type=Path)
    parser.add_argument('--firmware')
    parser.add_argument('--channel', choices=['prod', 'beta'], default='prod')
    parser.add_argument('--expect-board')
    parser.add_argument('--expect-version')
    parser.add_argument('--app')
    args = parser.parse_args()

    args.site.mkdir(parents=True, exist_ok=True)
    (args.site / '.nojekyll').touch()
    if args.firmware:
        publish_firmware(args.site, args.firmware, args.channel, args.expect_board, args.expect_version)
    if args.app:
        shutil.copy2(args.app, args.site / 'moto-buttons.apk')
        print(f'Published app ({Path(args.app).stat().st_size} bytes)')


if __name__ == '__main__':
    main()
