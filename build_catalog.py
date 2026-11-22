import json
import glob
import os
import requests
from pathlib import Path
from PIL import Image, ImageDraw
from io import BytesIO

CFG_FILE = Path('/home/leo/server/apps/jukebox/spotify_config.json')
AUDIO_DIR = Path('/home/leo/server/data/jukebox_audio')
CATALOG_FILE = Path('/home/leo/server/data/jukebox_catalog.json')
COVERS_DIR = Path('/home/leo/server/data/jukebox_covers')
COVERS_DIR.mkdir(parents=True, exist_ok=True)

with open(CFG_FILE, 'r') as f:
    cfg = json.load(f)

# Obter token de acesso
resp = requests.post(
    'https://accounts.spotify.com/api/token',
    data={
        'grant_type': 'refresh_token',
        'refresh_token': cfg['refresh_token'],
        'client_id': cfg['client_id'],
        'client_secret': cfg['client_secret'],
    },
    timeout=10,
)
access_token = resp.json().get('access_token')
if not access_token:
    print('Erro ao obter token do Spotify:', resp.text)
    exit(1)

headers = {'Authorization': f'Bearer {access_token}'}

# Listar todas as faixas .s16
audio_files = glob.glob(str(AUDIO_DIR / '*.s16'))
track_ids = [Path(f).stem for f in audio_files if not f.endswith('.part')]
print(f'Total de faixas para catalogar: {len(track_ids)}')

catalog = {}
if CATALOG_FILE.exists():
    try:
        with open(CATALOG_FILE, 'r', encoding='utf-8') as f:
            catalog = json.load(f)
    except Exception:
        pass

# Buscar metadados no Spotify em lotes de 50
missing_ids = [tid for tid in track_ids if tid not in catalog]
print(f'Faixas pendentes de metadados: {len(missing_ids)}')

for i in range(0, len(missing_ids), 50):
    batch = missing_ids[i:i+50]
    ids_param = ','.join(batch)
    r = requests.get(f'https://api.spotify.com/v1/tracks?ids={ids_param}', headers=headers, timeout=10)
    if r.status_code == 200:
        data = r.json()
        for t in data.get('tracks', []):
            if t:
                tid = t['id']
                cover_url = t['album']['images'][0]['url'] if t['album']['images'] else ''
                file_path = AUDIO_DIR / f'{tid}.s16'
                sz = round(os.path.getsize(file_path) / (1024 * 1024), 2) if file_path.exists() else 0
                catalog[tid] = {
                    'id': tid,
                    'title': t['name'],
                    'artist': ', '.join(a['name'] for a in t['artists']),
                    'album': t['album']['name'],
                    'cover_url': cover_url,
                    'duration_ms': t['duration_ms'],
                    'size_mb': sz
                }
    else:
        print(f'Erro no lote {i}: {r.status_code}')

with open(CATALOG_FILE, 'w', encoding='utf-8') as f:
    json.dump(catalog, f, ensure_ascii=False, indent=2)

print(f'Catalogo salvo com sucesso! Total de faixas catalogadas: {len(catalog)}')

# Gerar capas rotativas (.bin 8 frames 56x56) para as faixas do catalogo
print('Gerando capas rotativas...')
size = 56
num_frames = 8
angles = [int(k * (360 / num_frames)) for k in range(num_frames)]
mask = Image.new('L', (size, size), 0)
draw = ImageDraw.Draw(mask)
draw.ellipse((0, 0, size - 1, size - 1), fill=255)

covers_generated = 0
for tid, info in catalog.items():
    bin_path = COVERS_DIR / f'{tid}.bin'
    if bin_path.exists() and bin_path.stat().st_size == 50176:
        continue
    url = info.get('cover_url')
    if not url:
        continue
    try:
        c_resp = requests.get(url, timeout=5)
        if c_resp.status_code != 200:
            continue
        img = Image.open(BytesIO(c_resp.content)).convert('RGB')
        img = img.resize((size, size), Image.Resampling.LANCZOS)
        
        all_frames = bytearray()
        for angle in angles:
            rot = img.rotate(-angle, resample=Image.Resampling.BILINEAR)
            final_img = Image.new('RGB', (size, size), (12, 14, 18))
            final_img.paste(rot, (0, 0), mask)
            for y in range(size):
                for x in range(size):
                    r_px, g_px, b_px = final_img.getpixel((x, y))
                    rgb565 = ((r_px & 0xF8) << 8) | ((g_px & 0xFC) << 3) | (b_px >> 3)
                    all_frames.extend(rgb565.to_bytes(2, byteorder='big'))
        if len(all_frames) == 50176:
            with open(bin_path, 'wb') as bf:
                bf.write(all_frames)
            covers_generated += 1
    except Exception as e:
        print(f'Erro na capa {tid}: {e}')

print(f'Capas geradas: {covers_generated}')
