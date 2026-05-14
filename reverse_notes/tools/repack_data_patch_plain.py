#!/usr/bin/env python3
from __future__ import annotations

import argparse
import struct
import sys
import zlib
from dataclasses import dataclass
from pathlib import Path


"""
Usage:
  python reverse_notes/tools/repack_data_patch_plain.py data_patch.pak data_patch_plain.pak

Protected entries require PyCryptodome for AES:
  python -m pip install pycryptodome

With uv, install/use the dependency in one command:
  uv run --with pycryptodome python reverse_notes/tools/repack_data_patch_plain.py data_patch.pak data_patch_plain.pak
"""


PAK_MAGIC = 0x12345678
PAK_HEADER = struct.Struct("<III")
PAK_ENTRY = struct.Struct("<IIIBI")
PAK_HEADER_SIZE = PAK_HEADER.size
PAK_ENTRY_SIZE = PAK_ENTRY.size
RSA_BLOCK_SIZE = 128
AES_KEY_SIZE = 32

RSA_MODULUS = bytes.fromhex(
    "ac7bbbe36edf9ed75e3513569dad2d89"
    "081eb33d9cfb022ecabe1d3944b5e26d"
    "43e4d3a2b6c466a5c7fc9398f479d789"
    "47c57f21c66139ef00d4bd9e8b65ace5"
    "f315818d0a030695725c5ff4d989bb04"
    "d3de74b8f33823a66202ac9c0d44d728"
    "45713ab3a40b9c141a0de3842f0f4525"
    "8ffd5717c3f97ba50ec1d24acaf3f271"
)
RSA_EXPONENT = 65537
GCM_IV = b"5484258461056496\x00"


@dataclass(frozen=True)
class PakEntry:
    path_hash: int
    stored_size: int
    unpacked_size: int
    flags: int
    data_offset: int


@dataclass(frozen=True)
class LoadedPak:
    path: Path
    data: bytes
    entries: list[PakEntry]


def load_aes_ecb_encryptor():
    try:
        from Crypto.Cipher import AES  # type: ignore

        return lambda key: AES.new(key, AES.MODE_ECB).encrypt
    except ImportError:
        pass

    try:
        from Cryptodome.Cipher import AES  # type: ignore

        return lambda key: AES.new(key, AES.MODE_ECB).encrypt
    except ImportError:
        pass

    raise SystemExit(
        "AES backend missing. Install PyCryptodome with:\n"
        "  python -m pip install pycryptodome"
    )


make_aes_ecb_encrypt = None


def read_pak(path: Path) -> LoadedPak:
    data = path.read_bytes()
    if len(data) < PAK_HEADER_SIZE:
        raise ValueError(f"{path} is too small to be a pak")

    magic, data_start, entry_count = PAK_HEADER.unpack_from(data, 0)
    if magic != PAK_MAGIC:
        raise ValueError(f"{path} has unexpected magic 0x{magic:08X}")

    expected_data_start = PAK_HEADER_SIZE + entry_count * PAK_ENTRY_SIZE
    if data_start != expected_data_start:
        raise ValueError(
            f"{path} has data_start 0x{data_start:X}; expected 0x{expected_data_start:X}"
        )
    if entry_count == 0:
        raise ValueError(f"{path} has no entries")
    if data_start > len(data):
        raise ValueError(f"{path} entry table extends beyond EOF")

    entries: list[PakEntry] = []
    for index in range(entry_count):
        offset = PAK_HEADER_SIZE + index * PAK_ENTRY_SIZE
        path_hash, stored_size, unpacked_size, flags, data_offset = PAK_ENTRY.unpack_from(data, offset)
        if data_offset > len(data) or stored_size > len(data) - data_offset:
            raise ValueError(
                f"entry {index} points outside archive: "
                f"offset=0x{data_offset:X}, stored={stored_size}, file={len(data)}"
            )
        entries.append(PakEntry(path_hash, stored_size, unpacked_size, flags, data_offset))

    return LoadedPak(path=path, data=data, entries=entries)


def xor_block(left: bytes, right: bytes) -> bytes:
    return bytes(a ^ b for a, b in zip(left, right))


def shift_right_one(block: bytes) -> bytes:
    out = bytearray(16)
    carry = 0
    for i, value in enumerate(block):
        next_carry = value & 1
        out[i] = ((value >> 1) | (carry << 7)) & 0xFF
        carry = next_carry
    return bytes(out)


def ghash_multiply(x: bytes, h: bytes) -> bytes:
    z = bytes(16)
    v = h
    for bit_index in range(128):
        if x[bit_index // 8] & (0x80 >> (bit_index % 8)):
            z = xor_block(z, v)
        lsb = v[15] & 1
        v = shift_right_one(v)
        if lsb:
            v = bytes([v[0] ^ 0xE1]) + v[1:]
    return z


def ghash_update(y: bytes, h: bytes, block: bytes) -> bytes:
    if len(block) != 16:
        raise ValueError("GHASH blocks must be exactly 16 bytes")
    return ghash_multiply(xor_block(y, block), h)


def derive_gcm_j0(encrypt_block, iv: bytes) -> bytes:
    h = encrypt_block(bytes(16))
    y = bytes(16)

    for offset in range(0, len(iv), 16):
        block = iv[offset : offset + 16]
        if len(block) < 16:
            block = block + bytes(16 - len(block))
        y = ghash_update(y, h, block)

    length_block = bytearray(16)
    iv_bits = len(iv) * 8
    length_block[8:16] = iv_bits.to_bytes(8, "big")
    return ghash_update(y, h, bytes(length_block))


def increment_gcm_counter(counter: bytearray) -> None:
    for i in range(15, 11, -1):
        counter[i] = (counter[i] + 1) & 0xFF
        if counter[i] != 0:
            break


def aes_gcm_update_only(key: bytes, ciphertext: bytes) -> bytes:
    global make_aes_ecb_encrypt
    if make_aes_ecb_encrypt is None:
        make_aes_ecb_encrypt = load_aes_ecb_encryptor()
    encrypt_block = make_aes_ecb_encrypt(key)
    counter = bytearray(derive_gcm_j0(encrypt_block, GCM_IV))
    plaintext = bytearray(len(ciphertext))

    for offset in range(0, len(ciphertext), 16):
        increment_gcm_counter(counter)
        stream = encrypt_block(bytes(counter))
        chunk = ciphertext[offset : offset + 16]
        for i, value in enumerate(chunk):
            plaintext[offset + i] = value ^ stream[i]

    return bytes(plaintext)


def rsa_public_unwrap_key(block: bytes) -> bytes:
    if len(block) != RSA_BLOCK_SIZE:
        raise ValueError(f"RSA block must be {RSA_BLOCK_SIZE} bytes")

    modulus = int.from_bytes(RSA_MODULUS, "big")
    base = int.from_bytes(block, "big")
    plain = pow(base, RSA_EXPONENT, modulus).to_bytes(RSA_BLOCK_SIZE, "big")

    if plain[:2] != b"\x00\x01":
        raise ValueError("protected RSA block has invalid PKCS#1 type-1 header")

    delimiter = 2
    while delimiter < RSA_BLOCK_SIZE and plain[delimiter] == 0xFF:
        delimiter += 1
    if delimiter >= RSA_BLOCK_SIZE or plain[delimiter] != 0x00:
        raise ValueError("protected RSA block has invalid PKCS#1 padding")

    key = plain[delimiter + 1 :]
    if len(key) != AES_KEY_SIZE:
        raise ValueError("protected RSA block did not unwrap to a 32-byte AES key")
    return key


def unprotect_payload(stored: bytes) -> bytes:
    if len(stored) < RSA_BLOCK_SIZE:
        raise ValueError("protected payload is smaller than the RSA block")
    key = rsa_public_unwrap_key(stored[:RSA_BLOCK_SIZE])
    return aes_gcm_update_only(key, stored[RSA_BLOCK_SIZE:])


def decode_entry(pak: LoadedPak, entry: PakEntry) -> bytes:
    stored = pak.data[entry.data_offset : entry.data_offset + entry.stored_size]
    working = unprotect_payload(stored) if (entry.flags & 0x10) else stored

    if entry.flags & 0x07:
        decoded = zlib.decompress(working)
    else:
        decoded = working[: entry.unpacked_size]

    if len(decoded) != entry.unpacked_size:
        raise ValueError(
            f"decoded size mismatch for hash 0x{entry.path_hash:08X}: "
            f"got {len(decoded)}, expected {entry.unpacked_size}"
        )
    return decoded


def parse_name_list(raw: bytes) -> list[str]:
    if len(raw) < 4:
        raise ValueError("filename-list entry is too small")
    count = struct.unpack_from("<I", raw, 0)[0]
    names: list[str] = []
    offset = 4
    for _ in range(count):
        if offset + 2 > len(raw):
            raise ValueError("truncated filename-list length")
        size = struct.unpack_from("<H", raw, offset)[0]
        offset += 2
        if offset + size > len(raw):
            raise ValueError("truncated filename-list path")
        names.append(raw[offset : offset + size].decode("utf-8"))
        offset += size
    return names


def build_plain_pak(
    source: LoadedPak,
    output_path: Path,
    *,
    compress_level: int,
    store_if_larger: bool,
) -> None:
    decoded_payloads: list[bytes] = []
    output_payloads: list[bytes] = []
    output_flags: list[int] = []

    for index, entry in enumerate(source.entries):
        decoded = decode_entry(source, entry)
        decoded_payloads.append(decoded)

        is_name_list = index == len(source.entries) - 1
        if is_name_list:
            output_payloads.append(decoded)
            output_flags.append(0x00)
            continue

        compressed = zlib.compress(decoded, compress_level)
        if store_if_larger and len(compressed) >= len(decoded):
            output_payloads.append(decoded)
            output_flags.append(0x00)
        else:
            output_payloads.append(compressed)
            output_flags.append(0x01)

    names = parse_name_list(decoded_payloads[-1])
    expected_names = len(source.entries) - 1
    if len(names) != expected_names:
        raise ValueError(f"name list has {len(names)} names; expected {expected_names}")

    entry_count = len(source.entries)
    data_start = PAK_HEADER_SIZE + entry_count * PAK_ENTRY_SIZE
    offsets: list[int] = []
    cursor = data_start
    for payload in output_payloads:
        offsets.append(cursor)
        cursor += len(payload)
        if cursor > 0xFFFFFFFF:
            raise ValueError("output pak would exceed 4 GiB offset range")

    output_path.parent.mkdir(parents=True, exist_ok=True)
    with output_path.open("wb") as handle:
        handle.write(PAK_HEADER.pack(PAK_MAGIC, data_start, entry_count))
        for entry, decoded, payload, flags, data_offset in zip(
            source.entries, decoded_payloads, output_payloads, output_flags, offsets
        ):
            handle.write(
                PAK_ENTRY.pack(
                    entry.path_hash,
                    len(payload),
                    len(decoded),
                    flags,
                    data_offset,
                )
            )
        for payload in output_payloads:
            handle.write(payload)

    protected_count = sum(1 for entry in source.entries if entry.flags & 0x10)
    compressed_count = sum(1 for flags in output_flags[:-1] if flags == 0x01)
    raw_count = sum(1 for flags in output_flags[:-1] if flags == 0x00)
    print(f"read:  {source.path}")
    print(f"wrote: {output_path}")
    print(f"entries: {entry_count} ({len(names)} named resources + filename list)")
    print(f"decoded protected entries: {protected_count}")
    print(f"output resource flags: {compressed_count} compressed, {raw_count} raw")
    print("footer: omitted")


def parse_args(argv: list[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Repack Trials Fusion data_patch.pak as a standard unprotected pak. "
            "Protected 0x10/0x11 entries are decoded, then resource entries are "
            "written as normal 0x01 zlib payloads by default."
        )
    )
    parser.add_argument("input", type=Path, help="source data_patch.pak")
    parser.add_argument("output", type=Path, help="destination unprotected pak")
    parser.add_argument(
        "--level",
        type=int,
        default=9,
        choices=range(0, 10),
        metavar="0-9",
        help="zlib compression level for resource entries (default: 9)",
    )
    parser.add_argument(
        "--store-if-larger",
        action="store_true",
        help="write a resource entry as 0x00 raw if zlib would not make it smaller",
    )
    return parser.parse_args(argv)


def main(argv: list[str]) -> int:
    args = parse_args(argv)
    try:
        source = read_pak(args.input)
        build_plain_pak(
            source,
            args.output,
            compress_level=args.level,
            store_if_larger=args.store_if_larger,
        )
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
