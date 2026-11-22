import json
import os
import requests
from io import BytesIO
from PIL import Image, ImageDraw

TOKEN_FILE = r"c:\Users\Leonardo\OneDrive\Documentos\ideias\Jukebox\spotify_token.json"
DISCS_FILE = r"E:\discs.json"
COVERS_DIR = r"E:\covers"
os.makedirs(COVERS_DIR, exist_ok=True)

with open(TOKEN_FILE, "r", encoding="utf-8-sig") as f:
    cfg = json.load(f)

# Obter access token
token_res = requests.post(
    "https://accounts.spotify.com/api/token",
    data={
        "grant_type": "refresh_token",
        "refresh_token": cfg["refresh_token"],
        "client_id": cfg["client_id"],
        "client_secret": cfg["client_secret"],
    },
    timeout=10,
)
access_token = token_res.json().get("access_token")
if not access_token:
    print("Erro ao renovar token:", token_res.text)
    exit(1)

headers = {"Authorization": f"Bearer {access_token}"}

with open(DISCS_FILE, "r", encoding="utf-8") as f:
    discs = json.load(f)

size = 56
num_frames = 8
angles = [int(i * (360 / num_frames)) for i in range(num_frames)]
mask = Image.new("L", (size, size), 0)
draw = ImageDraw.Draw(mask)
draw.ellipse((0, 0, size - 1, size - 1), fill=255)

for uid, info in discs.items():
    uri = info.get("uri", "")
    if not uri.startswith("spotify:track:"):
        continue
    tid = uri.split(":")[-1]
    name = info.get("name", "")
    print(f"Buscando capa para {name} ({uid})...")
    
    t_res = requests.get(f"https://api.spotify.com/v1/tracks/{tid}", headers=headers, timeout=10)
    if t_res.status_code != 200:
        print(f"  Erro no Spotify ({t_res.status_code}): {t_res.text}")
        continue
    images = t_res.json().get("album", {}).get("images", [])
    if not images:
        continue
    img_url = images[0]["url"]
    
    img_res = requests.get(img_url, timeout=10)
    img = Image.open(BytesIO(img_res.content)).convert("RGB")
    img = img.resize((size, size), Image.Resampling.LANCZOS)
    
    all_frames = bytearray()
    for angle in angles:
        rot = img.rotate(-angle, resample=Image.Resampling.BILINEAR)
        final_img = Image.new("RGB", (size, size), (12, 14, 18))
        final_img.paste(rot, (0, 0), mask)
        for y in range(size):
            for x in range(size):
                r_px, g_px, b_px = final_img.getpixel((x, y))
                rgb565 = ((r_px & 0xF8) << 8) | ((g_px & 0xFC) << 3) | (b_px >> 3)
                all_frames.extend(rgb565.to_bytes(2, byteorder="big"))
                
    if len(all_frames) == 50176:
        out_path = os.path.join(COVERS_DIR, f"{uid}.bin")
        with open(out_path, "wb") as bf:
            bf.write(all_frames)
        print(f"  OK! Capa rotativa salva em: {out_path} ({len(all_frames)} bytes)")

print("\nConcluido! Todas as capas foram geradas.")
