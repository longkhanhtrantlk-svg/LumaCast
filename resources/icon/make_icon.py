"""Tao lumacast.ico tu lumacast.svg (chi chay khi doi icon): python -I resources/icon/make_icon.py
Can Pillow va Microsoft Edge (dung de ve SVG). File .ico duoc commit nen build khong can hai thu nay."""
import io, pathlib, struct, subprocess, tempfile
from PIL import Image

HERE = pathlib.Path(__file__).resolve().parent
SIZES = (16, 20, 24, 32, 40, 48, 64, 96, 128, 256)

def render(svg):
    edge = next(p for p in (r'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe',
                            r'C:\Program Files\Microsoft\Edge\Application\msedge.exe') if pathlib.Path(p).exists())
    with tempfile.TemporaryDirectory(ignore_cleanup_errors=True) as t:
        tmp = pathlib.Path(t)
        (tmp / 'icon.svg').write_bytes(svg.read_bytes())
        (tmp / 'icon.html').write_text('<body style="margin:0"><img src="icon.svg" width="1024" height="1024">', encoding='utf-8')
        out = tmp / 'icon.png'
        subprocess.run([edge, '--headless', '--disable-gpu', '--hide-scrollbars', '--force-device-scale-factor=1',
                        '--default-background-color=00000000',                 # giu trong suot (icon khong nen)
                        '--no-first-run', f'--user-data-dir={tmp / "edge"}', '--window-size=1100,1200',
                        f'--screenshot={out}', (tmp / 'icon.html').as_uri()], check=True, capture_output=True, timeout=90)
        return Image.open(out).convert('RGBA').crop((0, 0, 1024, 1024))

def entry(img):
    """256 px: PNG; nho hon: BMP 32 bit (moi cong cu Windows va NSIS deu doc duoc)."""
    n = img.width
    if n >= 256:
        buf = io.BytesIO()
        img.save(buf, 'PNG', optimize=True)
        return buf.getvalue()
    header = struct.pack('<IiiHHIIiiII', 40, n, n * 2, 1, 32, 0, 0, 0, 0, 0, 0)
    pixels = img.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes('raw', 'BGRA')
    stride = (n + 31) // 32 * 4
    mask = bytearray(stride * n)                               # mat na 1 bit cho phan trong suot han (duoi len tren)
    alpha = img.getchannel('A').transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes()
    for i, a in enumerate(alpha):
        if a == 0:
            mask[i // n * stride + i % n // 8] |= 0x80 >> (i % n % 8)
    return header + pixels + bytes(mask)

def write_ico(path, images):
    blobs = [entry(i) for i in images]
    offset = 6 + 16 * len(blobs)
    out = struct.pack('<HHH', 0, 1, len(blobs))
    for img, blob in zip(images, blobs):
        side = img.width % 256                                 # 256 ghi la 0
        out += struct.pack('<BBBBHHII', side, side, 0, 0, 1, 32, len(blob), offset)
        offset += len(blob)
    path.write_bytes(out + b''.join(blobs))

if __name__ == '__main__':
    big = render(HERE / 'lumacast.svg')
    write_ico(HERE / 'lumacast.ico', [big.resize((n, n), Image.Resampling.LANCZOS) for n in SIZES])
    print('lumacast.ico:', ', '.join(map(str, SIZES)))
