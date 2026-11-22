@echo off
title Ponte Spotify -> Jukebox ESP32
cd /d "%~dp0"
python ponte_audio.py --nome Jukebox --porta 3004
pause
