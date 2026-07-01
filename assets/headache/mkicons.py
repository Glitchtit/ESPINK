#!/usr/bin/env python3
"""Convert headache_1..5.png -> 1-bit XBM byte arrays for u8g2 DrawXBM.
Emits a C header and a verification PNG montage."""
from PIL import Image

SIZE = 72
THRESH = 150  # 0..255; pixels darker than this become ink (bit=1)
# per-image threshold tweaks (the calm avatar is dark; the KO is sparse)
THRESH_BY_N = {1: 120, 2: 150, 3: 150, 4: 150, 5: 150}

hdr = ['/* Auto-generated 1-bit headache severity icons (72x72) for u8g2 DrawXBM.',
       ' * Regenerate with scratchpad/mkicons.py from the HA /local/control/headache_*.png.',
       ' * XBM byte order: LSB = leftmost pixel; bit=1 -> ink (drawn). */',
       '#ifndef ESPINK_HEADACHE_ICONS_H',
       '#define ESPINK_HEADACHE_ICONS_H',
       '#include <stdint.h>',
       f'#define HEADACHE_ICON_W {SIZE}',
       f'#define HEADACHE_ICON_H {SIZE}',
       '']
row_bytes = (SIZE + 7) // 8
verif = Image.new('L', (SIZE * 5 + 24, SIZE), 200)

for n in range(1, 6):
    im = Image.open(f'headache_{n}.png').convert('RGBA')
    bg = Image.new('RGBA', im.size, (255, 255, 255, 255))
    im = Image.alpha_composite(bg, im).convert('L').resize((SIZE, SIZE), Image.LANCZOS)
    px = im.load()
    thr = THRESH_BY_N.get(n, THRESH)
    data = bytearray()
    for y in range(SIZE):
        for bx in range(row_bytes):
            b = 0
            for bit in range(8):
                x = bx * 8 + bit
                if x < SIZE and px[x, y] < thr:
                    b |= (1 << bit)
            data.append(b)
    # verification: render bits back
    for y in range(SIZE):
        for x in range(SIZE):
            byte = data[y * row_bytes + (x >> 3)]
            on = (byte >> (x & 7)) & 1
            verif.putpixel(((n - 1) * (SIZE + 4) + x, y), 0 if on else 255)
    arr = ', '.join(f'0x{b:02x}' for b in data)
    hdr.append(f'static const uint8_t headache_icon_{n}[] = {{{arr}}};')
    hdr.append('')

hdr.append('static const uint8_t *const headache_icons[5] = {')
hdr.append('    headache_icon_1, headache_icon_2, headache_icon_3, headache_icon_4, headache_icon_5,')
hdr.append('};')
hdr.append('#endif /* ESPINK_HEADACHE_ICONS_H */')

open('headache_icons.h', 'w').write('\n'.join(hdr))
verif.save('icons_verify.png')
print('wrote headache_icons.h and icons_verify.png; row_bytes=%d bytes/icon=%d' % (row_bytes, row_bytes * SIZE))
