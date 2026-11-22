import trimesh
import numpy as np
from pathlib import Path

# Diretório de saída para os arquivos STL
OUT_DIR = Path("c:/Users/Leonardo/OneDrive/Documentos/ideias/Jukebox/3D")
OUT_DIR.mkdir(parents=True, exist_ok=True)

print("==================================================")
print("   GERADOR CAD 3D - VITROLA JUKEBOX ESP32")
print("==================================================")

# ==============================================================================
# 1. PRATO GIRATÓRIO (TURNTABLE PLATTER)
# ==============================================================================
print("\n[1/3] Gerando Prato Giratório (Vitrola_Prato_Giratorio.stl)...")
# Prato principal: 88mm de diâmetro, 4mm de espessura
platter_base = trimesh.creation.cylinder(radius=44.0, height=4.0, sections=64)
# Translada para que a base fique em Z=0
platter_base.apply_translation([0, 0, 2.0])

# Borda externa sutil para acabamento clássico
rim = trimesh.creation.cylinder(radius=44.0, height=1.5, sections=64)
rim.apply_translation([0, 0, 4.0 - 0.75])

# Pino guia central para encaixar o disco de vinil (diâmetro 7.2mm, altura 8mm)
center_spindle = trimesh.creation.cylinder(radius=3.6, height=9.0, sections=32)
center_spindle.apply_translation([0, 0, 4.0 + 4.5])

# Hub inferior de acoplamento do motor (diâmetro 12mm, profundidade 6mm para baixo)
motor_hub = trimesh.creation.cylinder(radius=6.0, height=6.0, sections=32)
motor_hub.apply_translation([0, 0, -3.0])

# Furo do eixo do motor (diâmetro 2.2mm para encaixe sob pressão em eixo de motor DC)
motor_shaft_hole = trimesh.creation.cylinder(radius=1.1, height=10.0, sections=24)
motor_shaft_hole.apply_translation([0, 0, -2.0])

# União do prato
platter = platter_base.union([center_spindle, motor_hub])
platter = platter.difference(motor_shaft_hole)

platter_path = OUT_DIR / "Vitrola_Prato_Giratorio.stl"
platter.export(str(platter_path))
print(f" -> Salvo com sucesso: {platter_path} ({len(platter.faces)} faces)")


# ==============================================================================
# 2. TAMPA SUPERIOR / DECK DA VITROLA
# ==============================================================================
print("\n[2/3] Gerando Tampa Superior / Deck (Vitrola_Tampa_Superior.stl)...")
DECK_W = 160.0  # Largura (X)
DECK_D = 125.0  # Profundidade (Y)
DECK_H = 6.0    # Espessura do painel superior (Z)

# Placa base do deck
deck = trimesh.creation.box(extents=(DECK_W, DECK_D, DECK_H))
deck.apply_translation([0, 0, DECK_H / 2.0])

# 2.1 Rebaixo circular para o prato da vitrola (Lado esquerdo: X = -38mm)
# Diâmetro 94mm (folga de 3mm ao redor do prato de 88mm), profundidade 3mm
platter_well = trimesh.creation.cylinder(radius=47.0, height=3.5, sections=64)
platter_well.apply_translation([-38.0, 0.0, DECK_H - 1.75])

# Furo central para o eixo do motor atravessar (diâmetro 14mm)
spindle_through_hole = trimesh.creation.cylinder(radius=7.0, height=12.0, sections=32)
spindle_through_hole.apply_translation([-38.0, 0.0, DECK_H / 2.0])

# 2.2 Janela da Tela 2.8" TFT ILI9341 (Lado direito: X = +38mm, Y = +18mm)
# Abertura visível do display (70mm x 50mm)
screen_cutout = trimesh.creation.box(extents=(70.0, 50.0, 15.0))
screen_cutout.apply_translation([38.0, 18.0, DECK_H / 2.0])

# Rebaixo traseiro da moldura da tela para a placa encaixar rente (88mm x 52mm x 2.5mm)
screen_recess = trimesh.creation.box(extents=(88.0, 52.0, 3.0))
screen_recess.apply_translation([38.0, 18.0, 1.5])

# 2.3 Grade acústica do Alto-Falante (Lado direito, parte frontal: X = +38mm, Y = -35mm)
speaker_slots = []
for i in range(7):
    y_pos = -35.0 + (i - 3) * 5.0
    slot = trimesh.creation.box(extents=(42.0, 2.4, 15.0))
    slot.apply_translation([38.0, y_pos, DECK_H / 2.0])
    speaker_slots.append(slot)

# 2.4 Furos de fixação nos 4 cantos para parafusos M3 escareados (X = ±73mm, Y = ±55.5mm)
corner_screw_holes = []
for cx in [-73.0, 73.0]:
    for cy in [-55.5, 55.5]:
        hole = trimesh.creation.cylinder(radius=1.7, height=15.0, sections=24) # Furo M3 (3.4mm diam)
        hole.apply_translation([cx, cy, DECK_H / 2.0])
        # Escareado para a cabeça do parafuso sumir rente à superfície
        countersink = trimesh.creation.cylinder(radius=3.2, height=3.0, sections=24)
        countersink.apply_translation([cx, cy, DECK_H - 1.5])
        corner_screw_holes.extend([hole, countersink])

# Aplica todos os cortes na tampa
deck = deck.difference([platter_well, spindle_through_hole, screen_cutout, screen_recess])
deck = deck.difference(speaker_slots)
deck = deck.difference(corner_screw_holes)

deck_path = OUT_DIR / "Vitrola_Tampa_Superior.stl"
deck.export(str(deck_path))
print(f" -> Salvo com sucesso: {deck_path} ({len(deck.faces)} faces)")


# ==============================================================================
# 3. GABINETE INFERIOR / BASE DA VITROLA
# ==============================================================================
print("\n[3/3] Gerando Gabinete Inferior / Base (Vitrola_Gabinete_Base.stl)...")
BASE_W = 160.0
BASE_D = 125.0
BASE_H = 38.0
WALL_T = 2.5   # Espessura das paredes
FLOOR_T = 2.5  # Espessura do fundo

# Bloco sólido externo
base_outer = trimesh.creation.box(extents=(BASE_W, BASE_D, BASE_H))
base_outer.apply_translation([0, 0, BASE_H / 2.0])

# Cavidade interna vazada
inner_w = BASE_W - 2 * WALL_T
inner_d = BASE_D - 2 * WALL_T
inner_h = BASE_H - FLOOR_T + 2.0 # Vazado até o topo aberto
base_inner = trimesh.creation.box(extents=(inner_w, inner_d, inner_h))
base_inner.apply_translation([0, 0, FLOOR_T + inner_h / 2.0])

# 3.1 Pilares de fixação nos 4 cantos para parafusos M3
corner_pillars = []
pillar_holes = []
for cx in [-73.0, 73.0]:
    for cy in [-55.5, 55.5]:
        pillar = trimesh.creation.cylinder(radius=4.5, height=BASE_H - FLOOR_T, sections=32)
        pillar.apply_translation([cx, cy, FLOOR_T + (BASE_H - FLOOR_T) / 2.0])
        corner_pillars.append(pillar)

        # Furo piloto rosqueável para parafuso M3 (diâmetro 2.7mm)
        p_hole = trimesh.creation.cylinder(radius=1.35, height=25.0, sections=24)
        p_hole.apply_translation([cx, cy, BASE_H - 12.5])
        pillar_holes.append(p_hole)

# 3.2 Berço do Leitor RFID RC522 e Suporte do Motor (Lado Esquerdo: X = -38mm)
# Apoios internos para o leitor RFID ficar elevado logo abaixo da tampa superior
rfid_standoffs = []
for rx in [-38.0 - 24.0, -38.0 + 24.0]:
    for ry in [-16.0, 16.0]:
        st = trimesh.creation.cylinder(radius=3.5, height=BASE_H - FLOOR_T - 5.0, sections=24)
        st.apply_translation([rx, ry, FLOOR_T + (BASE_H - FLOOR_T - 5.0) / 2.0])
        rfid_standoffs.append(st)

# 3.3 Abertura traseira para conector USB-C (Parede traseira: Y = +62.5mm)
# Largura 13mm, Altura 7mm
usbc_port = trimesh.creation.box(extents=(13.0, 10.0, 7.5))
usbc_port.apply_translation([0.0, BASE_D / 2.0, FLOOR_T + 5.0])

# Construção do gabinete
base = base_outer.difference(base_inner)
base = base.union(corner_pillars)
base = base.union(rfid_standoffs)
base = base.difference(pillar_holes)
base = base.difference(usbc_port)

base_path = OUT_DIR / "Vitrola_Gabinete_Base.stl"
base.export(str(base_path))
print(f" -> Salvo com sucesso: {base_path} ({len(base.faces)} faces)")

print("\n==================================================")
print("   MODELAGEM 3D CONCLUIDA COM 100% DE SUCESSO!")
print(f"   Arquivos salvos em: {OUT_DIR}")
print("==================================================")
