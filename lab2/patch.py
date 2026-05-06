#!/usr/bin/env python3
from pathlib import Path
import sys

PATCH_OFFSET = 0x15AB
OLD_BYTES = bytes.fromhex("0f858e000000")
NEW_BYTES = bytes.fromhex("e98f00000090")


def patch_binary(path: Path) -> None:
    data = bytearray(path.read_bytes())

    cur = bytes(data[PATCH_OFFSET:PATCH_OFFSET + len(OLD_BYTES)])
    if cur == NEW_BYTES:
        print("Binary already patched.")
        return
    if cur != OLD_BYTES:
        raise RuntimeError(
            f"Unexpected bytes at offset 0x{PATCH_OFFSET:X}: {cur.hex()}"
        )

    data[PATCH_OFFSET:PATCH_OFFSET + len(NEW_BYTES)] = NEW_BYTES
    path.write_bytes(data)
    print("Patch applied successfully.")


if __name__ == "__main__":
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("lab2/hack_app/hack_app.patched")
    patch_binary(target)
