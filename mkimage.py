#!/usr/bin/env python3
"""
把 objcopy 产出的裸 bin 整理成可直接烧录的固件镜像，并做基本合法性检查。

当前的 Flash 布局是：
    0x08000000 ~ 0x08040000   应用程序（sector 0..5，256KB）
    0x08040000 ~ 0x08080000   littlefs（sector 6..7，各 128KB）

应用程序是连续的，且文件系统区在应用程序之后、不落在本镜像的地址范围内，
所以这里不再需要补 0xFF——直接烧这个镜像也不会碰到文件系统区，
重复烧固件不会破坏已写入的密钥和凭证。

脚本只做向量表校验，并拒绝超过应用程序区容量的镜像。
"""
import struct
import sys

FLASH_BASE = 0x08000000
APP_LIMIT = 0x08040000   # 应用程序区上限，越过就会覆盖文件系统区
APP_SIZE = APP_LIMIT - FLASH_BASE


def main():
    if len(sys.argv) != 3:
        print("usage: mkimage.py <raw.bin> <out.bin>")
        return 1
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, "rb") as f:
        data = bytearray(f.read())

    if len(data) < 8:
        print("image too small: %d bytes" % len(data))
        return 1

    msp, reset = struct.unpack_from("<II", data, 0)
    print("image: %d bytes, MSP=0x%08X, Reset=0x%08X" % (len(data), msp, reset))
    if not (0x20000000 <= msp <= 0x20020000):
        print("WARNING: MSP looks wrong, check linker script")
    if not (FLASH_BASE <= reset <= APP_LIMIT):
        print("WARNING: reset vector looks wrong")

    if len(data) > APP_SIZE:
        print("ERROR: image is %d bytes, exceeds the %d byte application area "
              "(would overwrite the filesystem)" % (len(data), APP_SIZE))
        return 1

    with open(dst, "wb") as f:
        f.write(bytes(data))
    print("wrote %s (%d bytes), %d bytes free in app area, FS region untouched"
          % (dst, len(data), APP_SIZE - len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
