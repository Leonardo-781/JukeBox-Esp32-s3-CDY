import json
import os
import re
import time
import unicodedata
from pathlib import Path
from flask import Flask, jsonify, render_template, request

try:
    import spotipy
    from spotipy.oauth2 import SpotifyOAuth
    SPOTIPY_AVAILABLE = True
except ImportError:
    SPOTIPY_AVAILABLE = False

BASE_DIR = Path(__file__).resolve().parent
DISCS_FILE = BASE_DIR / "discos.json"
CONFIG_FILE = BASE_DIR / "spotify_config.json"
CACHE_FILE = BASE_DIR / ".spotify_token_cache"

app = Flask(__name__)

# Estado atual da vitrola em memória
state = {
    "active_uid": None,
    "last_seen_uid": "1A2B3C4D",
    "playing": False,
    "title": "Aproxime um disco",
    "artist": "Vitrola Spotify",
    "disc_name": "Prato livre",
    "progress_sec": 0,
    "duration_sec": 180,
    "volume": 70,
    "preferred_device_id": "",
    "last_spotify_poll": 0.0,
}


def strip_accents(text: str) -> str:
    """Remove acentos para exibir sem caracteres quebrados na fonte padrão do CYD."""
    if not text:
        return ""
    norm = unicodedata.normalize("NFKD", str(text))
    clean = norm.encode("ascii", "ignore").decode("ascii")
    return clean.replace('"', "'")


def load_discs() -> dict:
    if not DISCS_FILE.exists():
        return {}
    try:
        return json.loads(DISCS_FILE.read_text(encoding="utf-8"))
    except Exception:
        return {}


def save_discs(discs: dict) -> None:
    DISCS_FILE.write_text(json.dumps(discs, indent=2, ensure_ascii=False), encoding="utf-8")


def load_spotify_config() -> dict:
    default_cfg = {
        "client_id": os.environ.get("SPOTIPY_CLIENT_ID", ""),
        "client_secret": os.environ.get("SPOTIPY_CLIENT_SECRET", ""),
        "redirect_uri": os.environ.get("SPOTIPY_REDIRECT_URI", "http://127.0.0.1:3003/callback"),
        "preferred_device_id": "",
    }
    if CONFIG_FILE.exists():
        try:
            saved = json.loads(CONFIG_FILE.read_text(encoding="utf-8"))
            default_cfg.update({k: v for k, v in saved.items() if v is not None})
        except Exception:
            pass
    return default_cfg


def save_spotify_config(cfg: dict) -> None:
    CONFIG_FILE.write_text(json.dumps(cfg, indent=2, ensure_ascii=False), encoding="utf-8")


def normalize_spotify_uri(raw: str) -> str:
    """Converte links https://open.spotify.com/... ou URIs para spotify:<tipo>:<id>."""
    raw = (raw or "").strip()
    if raw.startswith("spotify:"):
        return raw
    match = re.search(r"open\.spotify\.com/(?:intl-[a-z]+/)?(track|album|playlist|artist)/([a-zA-Z0-9]+)", raw)
    if match:
        kind, item_id = match.group(1), match.group(2)
        return f"spotify:{kind}:{item_id}"
    return raw


def get_spotify_client():
    """Retorna cliente Spotipy autenticado ou None se ainda não configurado."""
    if not SPOTIPY_AVAILABLE:
        return None
    cfg = load_spotify_config()
    if not cfg.get("client_id") or not cfg.get("client_secret"):
        return None
    try:
        auth_manager = SpotifyOAuth(
            client_id=cfg["client_id"],
            client_secret=cfg["client_secret"],
            redirect_uri=cfg["redirect_uri"],
            scope="user-read-playback-state user-modify-playback-state user-read-currently-playing",
            cache_path=str(CACHE_FILE),
            open_browser=True,
        )
        return spotipy.Spotify(auth_manager=auth_manager)
    except Exception as e:
        print(f"[SPOTIFY] Erro ao iniciar cliente: {e}")
        return None


def start_spotify_uri(uri: str) -> tuple[bool, str]:
    """Inicia reprodução de uma faixa, álbum, artista ou playlist no Spotify."""
    sp = get_spotify_client()
    if not sp:
        # Modo simulação local caso o Spotify ainda não esteja configurado
        state["playing"] = True
        state["progress_sec"] = 0
        state["duration_sec"] = 210
        state["title"] = "Modo Simulacao Ativo"
        state["artist"] = strip_accents(uri)
        return True, "Tocando em modo simulacao"

    try:
        cfg = load_spotify_config()
        device_id = cfg.get("preferred_device_id") or None

        # Se não houver device_id fixo, pega o primeiro dispositivo disponível (ex: PC ou JBL)
        if not device_id:
            devices = sp.devices().get("devices", [])
            if devices:
                active = [d for d in devices if d.get("is_active")]
                device_id = active[0]["id"] if active else devices[0]["id"]
            else:
                return False, "Nenhum aparelho Spotify aberto"

        if uri.startswith("spotify:track:"):
            sp.start_playback(device_id=device_id, uris=[uri])
        else:
            sp.start_playback(device_id=device_id, context_uri=uri)

        state["playing"] = True
        refresh_spotify_state(force=True)
        return True, "Tocando no Spotify!"
    except Exception as e:
        print(f"[SPOTIFY] Erro ao dar play: {e}")
        return False, "Abra o Spotify no PC/JBL"


def refresh_spotify_state(force: bool = False) -> None:
    now = time.time()
    if not force and (now - state["last_spotify_poll"] < 1.8):
        return
    state["last_spotify_poll"] = now

    sp = get_spotify_client()
    if not sp:
        return

    try:
        pb = sp.current_playback()
        if not pb:
            return
        state["playing"] = bool(pb.get("is_playing", False))
        device = pb.get("device") or {}
        if device.get("volume_percent") is not None:
            state["volume"] = int(device["volume_percent"])

        item = pb.get("item") or {}
        if item:
            state["title"] = strip_accents(item.get("name", "Sem titulo"))
            artists = ", ".join(a.get("name", "") for a in item.get("artists", []))
            state["artist"] = strip_accents(artists or "Spotify")
            state["progress_sec"] = int((pb.get("progress_ms") or 0) // 1000)
            state["duration_sec"] = max(1, int((item.get("duration_ms") or 180000) // 1000))
    except Exception as e:
        print(f"[SPOTIFY] Falha ao atualizar status: {e}")


# ==========================================
# ROTAS PARA O ESP32 CYD (VITROLA)
# ==========================================

@app.route("/api/disc/place", methods=["POST"])
def api_disc_place():
    data = request.get_json(silent=True) or {}
    uid = str(data.get("uid", "")).strip().upper()
    if not uid:
        return jsonify({"ok": False, "message": "UID invalido"}), 400

    state["active_uid"] = uid
    state["last_seen_uid"] = uid

    discs = load_discs()
    disc = discs.get(uid)

    if disc:
        disc_name = strip_accents(disc.get("name", f"Disco {uid}"))
        state["disc_name"] = disc_name
        ok, msg = start_spotify_uri(disc["uri"])
        return jsonify({
            "ok": ok,
            "registered": True,
            "uid": uid,
            "disc_name": disc_name,
            "message": strip_accents(msg),
        })
    else:
        state["disc_name"] = f"Novo ({uid})"
        state["playing"] = False
        return jsonify({
            "ok": True,
            "registered": False,
            "uid": uid,
            "disc_name": f"Novo ({uid})",
            "message": "Toque em GRAVAR p/ vincular",
        })


@app.route("/api/disc/remove", methods=["POST"])
def api_disc_remove():
    state["active_uid"] = None
    state["playing"] = False
    state["disc_name"] = "Prato livre"

    sp = get_spotify_client()
    if sp:
        try:
            sp.pause_playback()
        except Exception as e:
            print(f"[SPOTIFY] Aviso ao pausar: {e}")

    return jsonify({"ok": True, "message": "Disco removido e musica pausada"})


@app.route("/api/disc/bind", methods=["POST"])
def api_disc_bind():
    """Grava no disco RFID atual o álbum/playlist/música que estiver tocando no Spotify!"""
    data = request.get_json(silent=True) or {}
    uid = str(data.get("uid") or state.get("active_uid") or state.get("last_seen_uid") or "").strip().upper()
    if not uid:
        return jsonify({"ok": False, "message": "Nenhum disco detectado"}), 400

    sp = get_spotify_client()
    if not sp:
        return jsonify({
            "ok": False,
            "message": "Configure o Spotify no painel web",
        })

    try:
        pb = sp.current_playback()
        if not pb or not pb.get("item"):
            return jsonify({
                "ok": False,
                "message": "De Play em algo no Spotify antes!",
            })

        item = pb["item"]
        context = pb.get("context") or {}
        uri = context.get("uri") or item.get("uri")

        # Monta um nome amigável para o disco
        album_name = (item.get("album") or {}).get("name", "")
        track_name = item.get("name", "Disco Spotify")
        artists = ", ".join(a.get("name", "") for a in item.get("artists", []))

        if context.get("type") == "playlist":
            disc_name = f"Playlist: {track_name[:18]}"
        elif context.get("type") == "album" and album_name:
            disc_name = f"{album_name[:28]}"
        else:
            disc_name = f"{track_name[:18]} - {artists[:12]}"

        discs = load_discs()
        discs[uid] = {
            "name": disc_name,
            "uri": uri,
        }
        save_discs(discs)

        clean_name = strip_accents(disc_name)
        state["disc_name"] = clean_name
        state["active_uid"] = uid
        return jsonify({
            "ok": True,
            "uid": uid,
            "disc_name": clean_name,
            "uri": uri,
            "message": "Gravado no disco com sucesso!",
        })
    except Exception as e:
        print(f"[SPOTIFY] Erro ao gravar disco: {e}")
        return jsonify({"ok": False, "message": "Erro ao ler Spotify atual"})


@app.route("/api/player/<action>", methods=["POST"])
def api_player_action(action: str):
    sp = get_spotify_client()
    if not sp:
        if action == "toggle":
            state["playing"] = not state["playing"]
        elif action == "vol_up":
            state["volume"] = min(100, state["volume"] + 10)
        elif action == "vol_down":
            state["volume"] = max(0, state["volume"] - 10)
        return jsonify({"ok": True, "simulated": True})

    try:
        if action == "toggle":
            refresh_spotify_state(force=True)
            if state["playing"]:
                sp.pause_playback()
                state["playing"] = False
            else:
                sp.start_playback()
                state["playing"] = True
        elif action == "next":
            sp.next_track()
        elif action == "prev":
            sp.previous_track()
        elif action == "vol_up":
            state["volume"] = min(100, state["volume"] + 10)
            sp.volume(state["volume"])
        elif action == "vol_down":
            state["volume"] = max(0, state["volume"] - 10)
            sp.volume(state["volume"])
        refresh_spotify_state(force=True)
        return jsonify({"ok": True})
    except Exception as e:
        print(f"[SPOTIFY] Erro no comando {action}: {e}")
        return jsonify({"ok": False, "message": str(e)})


@app.route("/api/status", methods=["GET"])
def api_status():
    refresh_spotify_state(force=False)
    discs = load_discs()
    uid = state["active_uid"]
    return jsonify({
        "playing": state["playing"],
        "title": strip_accents(state["title"]),
        "artist": strip_accents(state["artist"]),
        "disc_name": strip_accents(state["disc_name"]),
        "progress_sec": state["progress_sec"],
        "duration_sec": state["duration_sec"],
        "volume": state["volume"],
        "active_uid": uid or "",
        "last_seen_uid": state["last_seen_uid"] or "",
        "registered": bool(uid and uid in discs),
    })


# ==========================================
# PAINEL WEB DE GERENCIAMENTO DOS DISCOS
# ==========================================

@app.route("/login")
def spotify_login():
    cfg = load_spotify_config()
    auth_manager = SpotifyOAuth(
        client_id=cfg["client_id"],
        client_secret=cfg["client_secret"],
        redirect_uri=cfg["redirect_uri"],
        scope="user-read-playback-state user-modify-playback-state user-read-currently-playing",
        cache_path=str(CACHE_FILE),
    )
    url = auth_manager.get_authorize_url()
    return f'<script>window.location.href="{url}";</script>'

@app.route("/")
def index():
    discs = load_discs()
    cfg = load_spotify_config()
    devices = []
    sp = get_spotify_client()
    spotify_connected = False
    if sp:
        try:
            devices = sp.devices().get("devices", [])
            spotify_connected = True
        except Exception:
            spotify_connected = False

    return render_template(
        "index.html",
        discs=discs,
        state=state,
        cfg=cfg,
        devices=devices,
        spotify_connected=spotify_connected,
    )


@app.route("/api/web/save_disc", methods=["POST"])
def web_save_disc():
    data = request.get_json(silent=True) or {}
    uid = str(data.get("uid", "")).strip().upper()
    name = str(data.get("name", "")).strip() or f"Disco {uid}"
    uri = normalize_spotify_uri(str(data.get("uri", "")))
    if not uid or not uri:
        return jsonify({"ok": False, "message": "Preencha o UID e o link/URI do Spotify"}), 400

    discs = load_discs()
    discs[uid] = {"name": name, "uri": uri}
    save_discs(discs)
    return jsonify({"ok": True, "discs": discs})


@app.route("/api/web/delete_disc", methods=["POST"])
def web_delete_disc():
    data = request.get_json(silent=True) or {}
    uid = str(data.get("uid", "")).strip().upper()
    discs = load_discs()
    if uid in discs:
        del discs[uid]
        save_discs(discs)
    return jsonify({"ok": True, "discs": discs})


@app.route("/api/web/save_config", methods=["POST"])
def web_save_config():
    data = request.get_json(silent=True) or {}
    cfg = load_spotify_config()
    cfg["client_id"] = str(data.get("client_id", "")).strip()
    cfg["client_secret"] = str(data.get("client_secret", "")).strip()
    cfg["redirect_uri"] = str(data.get("redirect_uri", "http://127.0.0.1:3003/callback")).strip()
    cfg["preferred_device_id"] = str(data.get("preferred_device_id", "")).strip()
    save_spotify_config(cfg)
    return jsonify({"ok": True})


@app.route("/callback")
def spotify_callback():
    code = request.args.get("code")
    if code:
        cfg = load_spotify_config()
        auth_manager = SpotifyOAuth(
            client_id=cfg["client_id"],
            client_secret=cfg["client_secret"],
            redirect_uri=cfg["redirect_uri"],
            scope="user-read-playback-state user-modify-playback-state user-read-currently-playing",
            cache_path=str(CACHE_FILE),
        )
        auth_manager.get_access_token(code)
    return '<script>window.location.href="/";</script>'


if __name__ == "__main__":
    print("\n==================================================")
    print("  SERVIDOR JUKEBOX VITROLA SPOTIFY INICIADO")
    print("  Abra no navegador: http://127.0.0.1:3003")
    print("==================================================\n")
    app.run(host="0.0.0.0", port=3003, debug=False)
