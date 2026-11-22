import re

APP_PATH = "/home/leo/server/apps/jukebox/app.py"

with open(APP_PATH, "r", encoding="utf-8") as f:
    content = f.read()

target = '''    cached_tracks = []
    audio_dir = Path("/home/leo/server/data/jukebox_audio")
    for f in sorted(audio_dir.glob("*.s16")):
        tid = f.stem
        name = ""
        for u, d in discs.items():
            if d.get("uri", "").endswith(tid):
                name = d.get("name", "")
                break
        cached_tracks.append({
            "track_id": tid,
            "name": name or f"Faixa {tid[:10]}...",
            "size_mb": round(f.stat().st_size / (1024 * 1024), 1),
            "uri": f"spotify:track:{tid}"
        })'''

replacement = '''    cached_tracks = []
    try:
        res = requests.get(f"{AUDIO_BRIDGE_URL}/cached_tracks", timeout=2)
        if res and res.status_code == 200:
            for item in res.json().get("tracks", []):
                tid = item["track_id"]
                name = ""
                for u, d in discs.items():
                    if d.get("uri", "").endswith(tid):
                        name = d.get("name", "")
                        break
                cached_tracks.append({
                    "track_id": tid,
                    "name": name or f"Faixa {tid[:10]}...",
                    "size_mb": item["size_mb"],
                    "uri": f"spotify:track:{tid}"
                })
    except Exception as e:
        print(f"[CACHE FETCH ERR] {e}")'''

if target in content:
    content = content.replace(target, replacement, 1)
    with open(APP_PATH, "w", encoding="utf-8") as f:
        f.write(content)
    print("app.py cache logic updated!")
else:
    print("Target not found in app.py!")
