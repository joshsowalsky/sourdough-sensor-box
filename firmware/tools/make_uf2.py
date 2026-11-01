"""Wrap a firmware .bin as a .uf2 for the Feather's TinyUF2 bootloader.

Copy the result onto the FTHRS3BOOT drive (double-press reset to get it) and
the bootloader writes it into the first app slot and restarts into it.

    python make_uf2.py sourdough_box.ino.bin sourdough_box.uf2
"""
import struct
import sys

FAMILY_ESP32S3 = 0xC47E5767
PAYLOAD = 256


def to_uf2(data, base=0):
    blocks = (len(data) + PAYLOAD - 1) // PAYLOAD
    out = bytearray()
    for i in range(blocks):
        chunk = data[i * PAYLOAD:(i + 1) * PAYLOAD]
        head = struct.pack("<IIIIIIII", 0x0A324655, 0x9E5D5157, 0x00002000,
                           base + i * PAYLOAD, len(chunk), i, blocks, FAMILY_ESP32S3)
        out += head + chunk.ljust(476, b"\0") + struct.pack("<I", 0x0AB16F30)
    return bytes(out)


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f:
        data = f.read()
    with open(sys.argv[2], "wb") as f:
        f.write(to_uf2(data))
    print("wrote", sys.argv[2], len(data), "bytes of firmware")
