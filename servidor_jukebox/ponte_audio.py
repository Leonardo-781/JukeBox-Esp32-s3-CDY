"""
PONTE DE AUDIO SPOTIFY -> ESP32-S3 (MAX98357A)
================================================

Cria um dispositivo Spotify Connect chamado "Jukebox" (via librespot).
Voce escolhe a musica no app do Spotify (celular ou PC), toca em
"Dispositivos" -> "Jukebox", e o audio sai no ESP32.

Fluxo:
  librespot (--backend pipe, PCM estereo 16-bit 44.1 kHz no stdout)
    -> esta ponte converte para MONO 16-bit 44.1 kHz
    -> servidor TCP na porta 3004 (o ESP32 conecta aqui)

Requisitos:
  - Spotify PREMIUM (Spotify Connect nao funciona na conta gratis)
  - librespot.exe (ver LEIA-ME_PONTE.txt)
  - Python 3.9+ (sem bibliotecas extras)

Uso:
  python ponte_audio.py
  python ponte_audio.py --librespot C:\\caminho\\librespot.exe --nome Jukebox --porta 3004
"""

import argparse
import os
import shutil
import socket
import subprocess
import sys
import threading
import time
from array import array
from pathlib import Path

SAMPLE_RATE = 44100
STEREO_FRAME_BYTES = 4          # 2 canais x 16 bit
CHUNK_FRAMES = 1024             # ~23 ms por bloco
CHUNK_BYTES = CHUNK_FRAMES * STEREO_FRAME_BYTES

BASE_DIR = Path(__file__).resolve().parent


def log(msg: str) -> None:
    print(time.strftime("[%H:%M:%S] ") + msg, flush=True)


def stereo_para_mono(data: bytes) -> bytes:
    """Converte PCM s16le estereo intercalado em s16le mono (media L+R)."""
    a = array("h")
    a.frombytes(data)
    if sys.byteorder != "little":
        a.byteswap()
    left = a[0::2]
    right = a[1::2]
    mono = array("h", [(l + r) >> 1 for l, r in zip(left, right)])
    if sys.byteorder != "little":
        mono.byteswap()
    return mono.tobytes()


class ClienteESP:
    """Mantem apenas UM ESP32 conectado por vez (o mais recente vence)."""

    def __init__(self):
        self._lock = threading.Lock()
        self._sock = None

    def trocar(self, sock):
        with self._lock:
            if self._sock is not None:
                try:
                    self._sock.close()
                except OSError:
                    pass
            self._sock = sock

    def enviar(self, data: bytes) -> bool:
        """Envia bloqueando (a lentidao do ESP segura o ritmo do librespot).
        Retorna False se nao houver cliente."""
        with self._lock:
            sock = self._sock
        if sock is None:
            return False
        try:
            sock.sendall(data)
            return True
        except OSError as e:
            log(f"[ESP] Conexao perdida ({e}).")
            with self._lock:
                if self._sock is sock:
                    self._sock = None
            try:
                sock.close()
            except OSError:
                pass
            return False


def servidor_tcp(porta: int, cliente: ClienteESP) -> None:
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", porta))
    srv.listen(2)
    log(f"[TCP] Aguardando o ESP32 na porta {porta}...")
    while True:
        sock, addr = srv.accept()
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        sock.settimeout(10)  # se o ESP travar por 10 s, derruba e espera reconectar
        log(f"[ESP] Conectado: {addr[0]}:{addr[1]}")
        cliente.trocar(sock)


def achar_librespot(caminho: str) -> str:
    if caminho:
        return caminho
    local = BASE_DIR / ("librespot.exe" if os.name == "nt" else "librespot")
    if local.exists():
        return str(local)
    no_path = shutil.which("librespot")
    if no_path:
        return no_path
    cargo = Path.home() / ".cargo" / "bin" / ("librespot.exe" if os.name == "nt" else "librespot")
    if cargo.exists():
        return str(cargo)
    return ""


def rodar_librespot(args, cliente: ClienteESP) -> None:
    exe = achar_librespot(args.librespot)
    if not exe:
        log("[ERRO] librespot nao encontrado. Veja LEIA-ME_PONTE.txt para instalar.")
        sys.exit(1)

    cache_dir = BASE_DIR / ".librespot_cache"
    cache_dir.mkdir(exist_ok=True)

    cmd = [
        exe,
        "--name", args.nome,
        "--backend", "pipe",          # PCM cru no stdout
        "--format", "S16",
        "--bitrate", str(args.bitrate),
        "--device-type", "speaker",
        "--initial-volume", str(args.volume),
        "--cache", str(cache_dir),
    ]

    while True:
        log(f"[SPOTIFY] Iniciando librespot como dispositivo '{args.nome}'...")
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=None, bufsize=0)
        log("[SPOTIFY] Pronto! No app do Spotify: Dispositivos -> " + args.nome)

        sobra = b""
        relogio = time.monotonic()
        tocando = False
        try:
            while True:
                bloco = proc.stdout.read(CHUNK_BYTES)
                if not bloco:
                    break  # librespot fechou
                if not tocando:
                    tocando = True
                    log("[AUDIO] Recebendo musica do Spotify.")
                bloco = sobra + bloco
                util = len(bloco) - (len(bloco) % STEREO_FRAME_BYTES)
                sobra = bloco[util:]
                if util == 0:
                    continue
                mono = stereo_para_mono(bloco[:util])

                if cliente.enviar(mono):
                    relogio = time.monotonic()
                else:
                    # Sem ESP conectado: descarta no ritmo real para a musica
                    # nao "andar" mais rapido que o normal.
                    relogio += (util // STEREO_FRAME_BYTES) / SAMPLE_RATE
                    espera = relogio - time.monotonic()
                    if espera > 0:
                        time.sleep(espera)
                    else:
                        relogio = time.monotonic()
        except KeyboardInterrupt:
            proc.terminate()
            raise
        finally:
            try:
                proc.kill()
            except OSError:
                pass

        log("[SPOTIFY] librespot encerrou. Reiniciando em 3 s...")
        time.sleep(3)


def main():
    p = argparse.ArgumentParser(description="Ponte Spotify Connect -> ESP32 (PCM mono TCP)")
    p.add_argument("--librespot", default="", help="Caminho do librespot.exe")
    p.add_argument("--nome", default="Jukebox", help="Nome que aparece no app do Spotify")
    p.add_argument("--porta", type=int, default=3004, help="Porta TCP do audio para o ESP32")
    p.add_argument("--bitrate", type=int, default=320, choices=[96, 160, 320])
    p.add_argument("--volume", type=int, default=80, help="Volume inicial 0-100")
    args = p.parse_args()

    cliente = ClienteESP()
    threading.Thread(target=servidor_tcp, args=(args.porta, cliente), daemon=True).start()
    try:
        rodar_librespot(args, cliente)
    except KeyboardInterrupt:
        log("Encerrado.")


if __name__ == "__main__":
    main()
