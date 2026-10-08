"""Check the resource bundle used by PreferenceLoader for the Settings icon."""
import argparse
import plistlib
import struct
from pathlib import Path


def validate(entry_path, resources):
    entry = plistlib.loads(entry_path.read_bytes())["entry"]
    icon = Path(entry["icon"])
    if icon.name != str(icon) or icon.suffix != ".png":
        raise ValueError("Settings icon must be a PNG name relative to the preference bundle")
    if entry.get("bundle") != "HotspotVPNDNSPrefs":
        raise ValueError("unexpected preference bundle")
    for scale in (1, 2, 3):
        suffix = "" if scale == 1 else f"@{scale}x"
        path = resources / (icon.stem + suffix + icon.suffix)
        raw = path.read_bytes()
        if raw[:8] != b"\x89PNG\r\n\x1a\n" or raw[12:16] != b"IHDR":
            raise ValueError(f"invalid icon PNG: {path.name}")
        if struct.unpack_from(">II", raw, 16) != (29 * scale, 29 * scale):
            raise ValueError(f"Settings icon must be 29 points: {path.name}")


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--entry", type=Path, default=root / "layout/Library/PreferenceLoader/Preferences/HotspotVPNDNS.plist")
    parser.add_argument("--resources", type=Path, default=root / "prefs/Resources")
    args = parser.parse_args()
    validate(args.entry, args.resources)
    print("Settings icon: preference bundle contains 29-point resources for 1x/2x/3x")
