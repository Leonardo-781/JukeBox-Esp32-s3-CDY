import os

path = "/home/leo/server/apps/jukebox/audio_bridge.py"
with open(path, "r", encoding="utf-8") as f:
    c = f.read()

target = 'if self.path == "/status":'
replacement = '''if self.path == "/cached_tracks":
            tracks = []
            for f in sorted(CACHE_DIR.glob("*.s16")):
                tracks.append({"track_id": f.stem, "size_mb": round(f.stat().st_size / (1024 * 1024), 1)})
            self._send_json(200, {"ok": True, "tracks": tracks})
        elif self.path == "/status":'''

if target in c and "/cached_tracks" not in c:
    c = c.replace(target, replacement, 1)
    with open(path, "w", encoding="utf-8") as f:
        f.write(c)
    print("audio_bridge.py updated successfully!")
else:
    print("Already updated or target not found.")
