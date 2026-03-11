#!/usr/bin/env python3
import sys
import os

def usage():
    print("Usage: merge_firmware.py <boot_bin> <app_bin> <out_bin> <app_offset_hex> [pad_size]")
    print("Example: merge_firmware.py obj/bootloader.bin obj/KeyBoard.bin combined.bin 0x2000")

if __name__ == '__main__':
    if len(sys.argv) < 5:
        usage()
        sys.exit(2)
    boot = sys.argv[1]
    app = sys.argv[2]
    out = sys.argv[3]
    try:
        app_offset = int(sys.argv[4], 0)
    except Exception:
        print('Invalid app_offset')
        sys.exit(2)
    pad_size = None
    if len(sys.argv) >= 6:
        pad_size = int(sys.argv[5], 0)

    if not os.path.isfile(boot):
        print('Boot file not found:', boot)
        sys.exit(3)
    if not os.path.isfile(app):
        print('App file not found:', app)
        sys.exit(4)

    boot_data = open(boot, 'rb').read()
    app_data = open(app, 'rb').read()

    # Compose output buffer
    out_size = app_offset + len(app_data)
    if pad_size is not None:
        out_size = max(out_size, pad_size)

    buf = bytearray([0xFF]) * out_size

    # place boot at 0
    buf[0:len(boot_data)] = boot_data
    # place app at offset
    buf[app_offset:app_offset+len(app_data)] = app_data

    with open(out, 'wb') as f:
        f.write(buf)

    print('Wrote', out, 'size', out_size)
    sys.exit(0)
