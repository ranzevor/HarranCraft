"""Generate minimal original PE structures in temporary test files."""

import struct


def image_bytes(*, dll: bool = True, exports: bool = True, version: bool = True) -> bytearray:
    data = bytearray(0xE00)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into(
        "<HHIIIHH", data, 0x84, 0x8664, 3, 123456789, 0, 0, 240, 0x22 | (0x2000 if dll else 0)
    )
    optional = 0x98
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<I", data, optional + 16, 0 if dll else 0x1000)
    struct.pack_into("<Q", data, optional + 24, 0x180000000)
    struct.pack_into("<II", data, optional + 32, 0x1000, 0x200)
    struct.pack_into("<II", data, optional + 56, 0x4000, 0x200)
    struct.pack_into("<H", data, optional + 68, 3)
    struct.pack_into("<I", data, optional + 108, 16)
    if exports:
        struct.pack_into("<II", data, optional + 112, 0x2000, 0x180)
    if version:
        struct.pack_into("<II", data, optional + 128, 0x3000, 0x200)
    for i, (name, virtual, rva, raw, offset, flags) in enumerate(
        (
            (b".text", 0x180, 0x1000, 0x200, 0x200, 0x60000020),
            (b".rdata", 0x800, 0x2000, 0x800, 0x400, 0x40000040),
            (b".rsrc", 0x200, 0x3000, 0x200, 0xC00, 0x40000040),
        )
    ):
        at = optional + 240 + i * 40
        data[at : at + len(name)] = name
        struct.pack_into("<IIII", data, at + 8, virtual, rva, raw, offset)
        struct.pack_into("<I", data, at + 36, flags)
    data[0x200:0x206] = bytes.fromhex("b843480000c3")
    if exports:
        struct.pack_into(
            "<IIHHIIIIIII", data, 0x400, 0, 0, 0, 0, 0x2080, 1, 2, 2, 0x2050, 0x2060, 0x2070
        )
        struct.pack_into("<II", data, 0x450, 0x1000, 0x2140)
        struct.pack_into("<II", data, 0x460, 0x20A0, 0x20C0)
        struct.pack_into("<HH", data, 0x470, 0, 1)
        for off, s in (
            (0x480, b"probe.dll\0"),
            (0x4A0, b"hc_probe\0"),
            (0x4C0, b"hc_sleep\0"),
            (0x540, b"KERNEL32.Sleep\0"),
        ):
            data[off : off + len(s)] = s
    if version:
        for off, name, target in ((0, 16, 24), (24, 1, 48), (48, 1033, 72)):
            struct.pack_into("<HH", data, 0xC00 + off + 12, 0, 1)
            struct.pack_into(
                "<II", data, 0xC00 + off + 16, name, target | (0x80000000 if off < 48 else 0)
            )
        struct.pack_into("<IIII", data, 0xC48, 0x3080, 92, 0, 0)
        struct.pack_into("<HHH", data, 0xC80, 92, 52, 0)
        key = "VS_VERSION_INFO\0".encode("utf-16-le")
        data[0xC86 : 0xC86 + len(key)] = key
        struct.pack_into("<IIII", data, 0xCA8, 0xFEEF04BD, 0x10000, (1 << 16) | 55, 0)
    return data
