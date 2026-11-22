import re

APP_PATH = "/home/leo/server/apps/jukebox/app.py"

with open(APP_PATH, "r", encoding="utf-8") as f:
    content = f.read()

new_index_and_search = '''@app.route("/")
def index():
    sync_spotify_status(force=True)
    discs = load_discs()
    cfg = load_config()

    devices = []
    res = spotify_api_request("GET", "me/player/devices")
    if res and res.status_code == 200:
        devices = res.json().get("devices", [])

    cached_tracks = []
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
        })

    return render_template(
        "index.html",
        discs=discs,
        state=state,
        cfg=cfg,
        devices=devices,
        cached_tracks=cached_tracks,
    )


@app.route("/api/spotify/search", methods=["GET"])
def api_spotify_search():
    query = request.args.get("q", "").strip()
    if not query:
        return jsonify([])
    res = spotify_api_request("GET", f"search?type=track&limit=8&q={requests.utils.quote(query)}")
    if res and res.status_code == 200:
        items = (res.json().get("tracks") or {}).get("items") or []
        results = []
        for it in items:
            images = it.get("album", {}).get("images", [])
            results.append({
                "id": it.get("id"),
                "name": it.get("name"),
                "artist": ", ".join(a["name"] for a in it.get("artists", [])),
                "album": it.get("album", {}).get("name"),
                "cover_url": images[0]["url"] if images else "",
                "uri": it.get("uri")
            })
        return jsonify(results)
    return jsonify([])
'''

# Substitui @app.route("/") até @app.route("/api/web/save_disc")
pattern = r'@app\.route\("/"\)[\s\S]*?@app\.route\("/api/web/save_disc"'
replacement = new_index_and_search + '\n\n@app.route("/api/web/save_disc"'

if re.search(pattern, content):
    content = re.sub(pattern, replacement, content, count=1)
    with open(APP_PATH, "w", encoding="utf-8") as f:
        f.write(content)
    print("app.py atualizado com sucesso!")
else:
    print("Nao foi possivel localizar o padrao em app.py")
