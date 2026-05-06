#!/usr/bin/env python3
import hashlib
import ctypes
import mmap


def cpuid_leaf_1():
    code = bytes.fromhex(
        # mov eax, 1; cpuid; store eax/ebx/ecx/edx into [rdi + offsets]; ret
        "b801000000"
        "0fa2"
        "8907"
        "895f04"
        "894f08"
        "89570c"
        "c3"
    )
    mem = mmap.mmap(
        -1,
        mmap.PAGESIZE,
        flags=mmap.MAP_PRIVATE | mmap.MAP_ANONYMOUS,
        prot=mmap.PROT_READ | mmap.PROT_WRITE | mmap.PROT_EXEC,
    )
    mem.write(code)

    out = (ctypes.c_uint32 * 4)()
    fn = ctypes.CFUNCTYPE(None, ctypes.POINTER(ctypes.c_uint32))(ctypes.addressof(ctypes.c_char.from_buffer(mem)))
    fn(out)
    eax, ebx, ecx, edx = [int(v) for v in out]
    mem.close()
    return eax, ebx, ecx, edx


def bswap32(x: int) -> int:
    return int.from_bytes(x.to_bytes(4, "little"), "big")


def generate_key() -> tuple[str, str]:
    eax, _, _, edx = cpuid_leaf_1()
    hwid_hi = bswap32(eax)
    hwid_lo = bswap32(edx)
    psn = f"{hwid_hi:08X}{hwid_lo:08X}"

    digest = hashlib.md5(psn.encode("ascii")).digest()
    license_key = "".join(f"{b:02x}" for b in digest[::-1])
    return psn, license_key


if __name__ == "__main__":
    hwid, key = generate_key()
    print(f"HWID: {hwid}")
    print(f"License key: {key}")
