import numpy as np
import trimesh
from pathlib import Path
from manifold3d import Manifold, CrossSection

OUT_DIR = Path("c:/Users/Leonardo/OneDrive/Documentos/ideias/Jukebox/3D")
OUT_DIR.mkdir(parents=True, exist_ok=True)

print("======================================================================")
print("  GERADOR 3D PROFISSIONAL v2 - CONSOLE VITROLA JUKEBOX ESP32")
print("  Melhorias Implementadas:")
print("  - Dimensões aprimoradas: 126mm (largura) x 150mm (profundidade) x 106mm (altura)")
print("  - Encaixe específico para MOTOR AMARELO TT (eixo 5.4mm com duplo chanfro 3.7mm)")
print("  - Chave Liga/Desliga Mini Rocker (KCD11) na traseira ao lado do USB")
print("  - Furação frontal para ENCODER ROTATIVO (Volume / Play / Pause)")
print("  - Botão Knob Retrô Recartilhado personalizado para o Encoder")
print("  - Suportes internos: Tela 2.8\", Falante I2S, RC522, Motor TT, ESP32, MX1508, DAC")
print("======================================================================")

# ==============================================================================
# DIMENSÕES OTIMIZADAS DO GABINETE (MM)
# ==============================================================================
WIDTH   = 126.0   # Largura externa total (X: -63.0 a +63.0 mm)
DEPTH   = 150.0   # Profundidade total (Y: 0 a 150.0 mm)
H_REAR  = 106.0   # Altura traseira máxima
H_FRONT = 46.0    # Altura do degrau frontal (com folga extra para o motor TT)
WALL    = 2.8     # Espessura das paredes estruturais

# Transições do perfil em degrau (Plano Y-Z):
Y_APEX_REAR   = 14.0   # Topo traseiro plano (Y: 0 a 14 mm)
Y_SHELF_START = 56.0   # Fim da rampa da tela e início do deck do vinil

# Geometria da Rampa da Tela:
dy_ramp = Y_SHELF_START - Y_APEX_REAR  # 42.0 mm
dz_ramp = H_REAR - H_FRONT             # 60.0 mm
RAMP_ANGLE_RAD = np.arctan2(dz_ramp, dy_ramp) # ~55.0 graus
RAMP_ANGLE_DEG = np.degrees(RAMP_ANGLE_RAD)
RAMP_MID_Y = (Y_APEX_REAR + Y_SHELF_START) / 2.0 # 35.0 mm
RAMP_MID_Z = (H_REAR + H_FRONT) / 2.0            # 76.0 mm

# Deck do Vinil (Horizontal):
shelf_depth = DEPTH - Y_SHELF_START # 94.0 mm
platter_cx  = 0.0
platter_cy  = Y_SHELF_START + (shelf_depth / 2.0) # 103.0 mm (margem simétrica de 6mm na frente e atrás)

print(f" -> Gabinete: {WIDTH}mm (largura) x {DEPTH}mm (profundidade) x {H_REAR}mm (altura)")
print(f" -> Rampa Display: Inclinacao {RAMP_ANGLE_DEG:.1f}°, Comprimento {np.sqrt(dy_ramp**2 + dz_ramp**2):.1f}mm")
print(f" -> Deck Vinil: Comprimento {shelf_depth}mm, Centro do Prato Y={platter_cy:.1f}mm")

def save_stl(manifold_obj: Manifold, filename: str) -> Path:
    mesh_m = manifold_obj.to_mesh()
    verts = mesh_m.vert_properties[:, :3]
    faces = mesh_m.tri_verts
    tri = trimesh.Trimesh(vertices=verts, faces=faces)
    
    out_file = OUT_DIR / filename
    tri.export(str(out_file))
    vol_cm3 = tri.volume / 1000.0
    dims = tri.bounds[1] - tri.bounds[0]
    print(f" [OK] {filename}")
    print(f"      Faces: {len(faces)} | Volume: {vol_cm3:.1f} cm3 | Dimensoes: {dims[0]:.1f} x {dims[1]:.1f} x {dims[2]:.1f} mm")
    return out_file

# ==============================================================================
# 1. GABINETE CONSOLE (COM ENCODER, CHAVE LIGA/DESLIGA E MOTOR TT)
# ==============================================================================
print("\n[1/4] Gerando Vitrola_Console_Gabinete.stl...")

# 1.1 Perfil Externo Sólido
poly_outer_2d = [
    [0.0, 0.0],
    [DEPTH, 0.0],
    [DEPTH, H_FRONT],
    [Y_SHELF_START, H_FRONT],
    [Y_APEX_REAR, H_REAR],
    [0.0, H_REAR]
]
cs_outer = CrossSection([poly_outer_2d])
body_outer = Manifold.extrude(cs_outer, WIDTH, n_divisions=0)
T_orient = np.array([
    [0, 0, 1, -WIDTH / 2.0],
    [1, 0, 0, 0.0],
    [0, 1, 0, 0.0]
], dtype=float)
body_outer = body_outer.transform(T_orient)

# 1.2 Cavidade Oca Interna
poly_inner_2d = [
    [WALL, 0.0],
    [DEPTH - WALL, 0.0],
    [DEPTH - WALL, H_FRONT - WALL],
    [Y_SHELF_START - WALL * 0.7, H_FRONT - WALL],
    [Y_APEX_REAR + WALL * 0.7, H_REAR - WALL],
    [WALL, H_REAR - WALL]
]
cs_inner = CrossSection([poly_inner_2d])
body_cavity = Manifold.extrude(cs_inner, WIDTH - 2 * WALL, n_divisions=0)
T_orient_inner = np.array([
    [0, 0, 1, -WIDTH / 2.0 + WALL],
    [1, 0, 0, 0.0],
    [0, 1, 0, 0.0]
], dtype=float)
body_cavity = body_cavity.transform(T_orient_inner)

console = body_outer - body_cavity

# 1.3 Rebaixo Circular do Prato de Vinil no Deck (Diâmetro 86mm x 2.2mm)
platter_well = Manifold.cylinder(height=2.2, radius_low=43.0, radius_high=43.0, circular_segments=64)
platter_well = platter_well.translate([platter_cx, platter_cy, H_FRONT - 2.2])
console = console - platter_well

# Furo do eixo central do motor (diâmetro 14.0 mm)
spindle_hole = Manifold.cylinder(height=12.0, radius_low=7.0, radius_high=7.0, circular_segments=36)
spindle_hole = spindle_hole.translate([platter_cx, platter_cy, H_FRONT - 8.0])
console = console - spindle_hole

# 1.4 Moldura em Degrau para Display TFT 2.8"
screen_aperture = Manifold.cube([60.0, 44.0, 18.0], center=True)
screen_aperture = screen_aperture.rotate([-(90.0 - RAMP_ANGLE_DEG), 0.0, 0.0])
screen_aperture = screen_aperture.translate([0.0, RAMP_MID_Y, RAMP_MID_Z])
console = console - screen_aperture

screen_seat = Manifold.cube([72.0, 51.0, 3.2], center=True)
screen_seat = screen_seat.rotate([-(90.0 - RAMP_ANGLE_DEG), 0.0, 0.0])
dy_seat = 1.0 * np.sin(RAMP_ANGLE_RAD)
dz_seat = 1.0 * np.cos(RAMP_ANGLE_RAD)
screen_seat = screen_seat.translate([0.0, RAMP_MID_Y - dy_seat, RAMP_MID_Z - dz_seat])
console = console - screen_seat

# 1.5 Torres Internas de Fixação do Display TFT 2.8" (4 Torres M2.5)
for sx in [-39.0, 39.0]:
    for sy_rel in [-22.0, 22.0]:
        post = Manifold.cylinder(height=6.5, radius_low=3.8, radius_high=3.2, circular_segments=20)
        post = post.translate([0.0, 0.0, -6.5])
        hole = Manifold.cylinder(height=8.0, radius_low=1.1, radius_high=1.1, circular_segments=16)
        hole = hole.translate([0.0, 0.0, -7.0])
        p_comb = (post - hole).rotate([-(90.0 - RAMP_ANGLE_DEG), 0.0, 0.0])
        
        dy_p = sy_rel * np.cos(RAMP_ANGLE_RAD)
        dz_p = -sy_rel * np.sin(RAMP_ANGLE_RAD)
        p_comb = p_comb.translate([sx, RAMP_MID_Y + dy_p, RAMP_MID_Z + dz_p])
        console = console + p_comb

# 1.6 Grade Acústica Frontal para o Alto-Falante (Centralizado à esquerda: X = -14.0 mm)
speaker_cx = -14.0
for i in range(5):
    z_slot = 15.0 + i * 5.2
    slot = Manifold.cube([48.0, 10.0, 2.5], center=True)
    slot = slot.translate([speaker_cx, DEPTH, z_slot])
    console = console - slot

# 1.7 Torres de Fixação do Alto-Falante (Espaçamento 70mm X, 20mm Z)
for spx_rel in [-35.0, 35.0]:
    for spz in [15.5, 33.5]:
        spx = speaker_cx + spx_rel
        sp_post = Manifold.cylinder(height=6.5, radius_low=3.8, radius_high=3.2, circular_segments=20)
        sp_post = sp_post.rotate([90.0, 0.0, 0.0])
        sp_post = sp_post.translate([spx, DEPTH - WALL, spz])
        
        sp_hole = Manifold.cylinder(height=8.0, radius_low=1.1, radius_high=1.1, circular_segments=16)
        sp_hole = sp_hole.rotate([90.0, 0.0, 0.0])
        sp_hole = sp_hole.translate([spx, DEPTH - WALL + 0.5, spz])
        console = (console + sp_post) - sp_hole

# 1.8 Furação Frontal para o ENCODER ROTATIVO (Volume/Controle no painel frontal direito)
# Posicionado em X = +43.0 mm, Z = 24.5 mm (altura central frontal)
enc_x = 43.0
enc_z = 24.5
# Furo rosqueável do eixo M7 (7.2 mm de diâmetro)
enc_hole = Manifold.cylinder(height=12.0, radius_low=3.6, radius_high=3.6, circular_segments=32).rotate([90.0, 0.0, 0.0])
enc_hole = enc_hole.translate([enc_x, DEPTH + 2.0, enc_z])
# Entalhe anti-rotação (pino guia padrão de encoders KY-040 / EC11)
enc_notch = Manifold.cube([2.2, 10.0, 2.2], center=True).translate([enc_x - 5.5, DEPTH, enc_z])
console = console - enc_hole - enc_notch

# 1.9 Berço de Fixação do MOTOR AMARELO TT sob o Deck
# O motor amarelo TT mede 22.4mm de largura x 19mm de espessura.
# Furos de fixação M3 espaçados em 34.0mm ao longo de Y
for bx in [-12.5, 12.5]:
    bracket = Manifold.cube([4.5, 42.0, 16.0], center=True).translate([bx, platter_cy, H_FRONT - WALL - 8.0])
    console = console + bracket

for by_rel in [-17.0, 17.0]:
    b_hole = Manifold.cylinder(height=32.0, radius_low=1.6, radius_high=1.6, circular_segments=20).rotate([0, 90, 0])
    b_hole = b_hole.translate([0.0, platter_cy + by_rel, H_FRONT - WALL - 10.0])
    console = console - b_hole

# 1.10 Trilhos Guia de Encaixe para o Leitor RFID RC522 (Sob o Deck)
rail_l = Manifold.cube([3.0, 60.0, 4.0], center=True).translate([-22.0, platter_cy, H_FRONT - WALL - 2.0])
rail_r = Manifold.cube([3.0, 60.0, 4.0], center=True).translate([ 22.0, platter_cy, H_FRONT - WALL - 2.0])
lip_l  = Manifold.cube([2.0, 60.0, 1.5], center=True).translate([-20.0, platter_cy, H_FRONT - WALL - 4.0])
lip_r  = Manifold.cube([2.0, 60.0, 1.5], center=True).translate([ 20.0, platter_cy, H_FRONT - WALL - 4.0])
console = console + rail_l + rail_r + lip_l + lip_r

# 1.11 Traseira: Conector USB-C E Chave Liga/Desliga Mini Rocker (KCD11)
# USB-C no lado esquerdo (X = -20.0 mm)
usb_cutout = Manifold.cube([14.0, 10.0, 8.5], center=True).translate([-20.0, 0.0, 6.5])
# Chave Gangorra KCD11 no lado direito (X = +20.0 mm) - recorte padrão 13.5 x 9.0 mm
switch_cutout = Manifold.cube([13.5, 10.0, 9.2], center=True).translate([20.0, 0.0, 13.0])
console = console - usb_cutout - switch_cutout

# 1.12 4 Colunas Robustas nos Cantos para Parafusos M3 da Base
for cx in [-WIDTH / 2.0 + 8.5, WIDTH / 2.0 - 8.5]:
    for cy in [10.0, DEPTH - 10.0]:
        h_col = (H_FRONT - WALL) if cy > 60 else (H_REAR - WALL - 15.0)
        col = Manifold.cylinder(height=h_col, radius_low=4.5, radius_high=4.0, circular_segments=24)
        col = col.translate([cx, cy, 0.0])
        furo_m3 = Manifold.cylinder(height=18.0, radius_low=1.3, radius_high=1.3, circular_segments=20).translate([cx, cy, 0.0])
        console = (console + col) - furo_m3

save_stl(console, "Vitrola_Console_Gabinete.stl")


# ==============================================================================
# 2. TAMPA INFERIOR (ESP32, PONTE H, DAC I2S, VENTILAÇÃO E PÉS)
# ==============================================================================
print("\n[2/4] Gerando Vitrola_Tampa_Fundo.stl...")

plate_w = WIDTH - 0.8
plate_d = DEPTH - 0.8
plate_t = 2.4

plate = Manifold.cube([plate_w, plate_d, plate_t], center=True)
plate = plate.translate([0.0, DEPTH / 2.0, plate_t / 2.0])

# 2.1 4 Furos Escareados M3 para fixação no gabinete
for cx in [-WIDTH / 2.0 + 8.5, WIDTH / 2.0 - 8.5]:
    for cy in [10.0, DEPTH - 10.0]:
        furo = Manifold.cylinder(height=10.0, radius_low=1.7, radius_high=1.7, circular_segments=20).translate([cx, cy, -2.0])
        escareado = Manifold.cylinder(height=1.5, radius_low=3.3, radius_high=3.3, circular_segments=24).translate([cx, cy, 0.0])
        plate = plate - furo - escareado

# 2.2 Torres de Fixação do ESP32 (38 Pinos) - Alinhado com a porta USB (X = -20.0 mm)
esp_center_x = -20.0
esp_center_y = 38.0
for ex_rel in [-11.5, 11.5]:
    for ey_rel in [-24.5, 24.5]:
        ex = esp_center_x + ex_rel
        esp_post = Manifold.cylinder(height=5.5, radius_low=3.2, radius_high=2.8, circular_segments=20)
        esp_post = esp_post.translate([ex, esp_center_y + ey_rel, plate_t])
        esp_hole = Manifold.cylinder(height=6.5, radius_low=1.1, radius_high=1.1, circular_segments=16)
        esp_hole = esp_hole.translate([ex, esp_center_y + ey_rel, plate_t])
        plate = (plate + esp_post) - esp_hole

# 2.3 Pinos de Fixação da Mini Ponte H (MX1508)
# Posicionada no lado direito traseiro (X = +25.0 mm)
mx_cx = 25.0
mx_cy = 38.0
pin1 = Manifold.cylinder(height=4.2, radius_low=1.4, radius_high=1.1, circular_segments=16).translate([mx_cx - 8.0, mx_cy, plate_t])
pin2 = Manifold.cylinder(height=4.2, radius_low=1.4, radius_high=1.1, circular_segments=16).translate([mx_cx + 8.0, mx_cy, plate_t])
plate = plate + pin1 + pin2

# 2.4 Pinos de Fixação da Placa DAC I2S (MAX98357A / NS4168)
# Posicionada na frente, perto do alto-falante (X = -14.0 mm, Y = 100.0 mm)
dac_cx = -14.0
dac_cy = 100.0
dac_pin1 = Manifold.cylinder(height=4.2, radius_low=1.4, radius_high=1.1, circular_segments=16).translate([dac_cx - 7.5, dac_cy, plate_t])
dac_pin2 = Manifold.cylinder(height=4.2, radius_low=1.4, radius_high=1.1, circular_segments=16).translate([dac_cx + 7.5, dac_cy, plate_t])
plate = plate + dac_pin1 + dac_pin2

# 2.5 Aletas de Ventilação Térmica / Acústica no Fundo
for v in range(8):
    y_v = 72.0 + v * 6.5
    vent = Manifold.cube([52.0, 2.5, 10.0], center=True).translate([15.0, y_v, plate_t / 2.0])
    plate = plate - vent

# 2.6 Recessos para 4 Pés de Borracha / Silicone
for fx in [-WIDTH / 2.0 + 18.0, WIDTH / 2.0 - 18.0]:
    for fy in [20.0, DEPTH - 20.0]:
        recesso = Manifold.cylinder(height=0.9, radius_low=5.5, radius_high=5.5, circular_segments=24).translate([fx, fy, -0.1])
        plate = plate - recesso

save_stl(plate, "Vitrola_Tampa_Fundo.stl")


# ==============================================================================
# 3. PRATO GIRATÓRIO (COM ENCAIXE ESPECÍFICO DO MOTOR AMARELO TT)
# ==============================================================================
print("\n[3/4] Gerando Vitrola_Prato_Giratorio.stl...")

# 3.1 Disco do Prato (Diâmetro 82mm, altura 3.5mm)
platter_base = Manifold.cylinder(height=3.5, radius_low=41.0, radius_high=41.0, circular_segments=64)

# 3.2 Borda Estética Chanfrada
platter_rim = Manifold.cylinder(height=1.0, radius_low=41.0, radius_high=39.5, circular_segments=64).translate([0.0, 0.0, 3.5])

# 3.3 Pino Central de Vinil (Diâmetro 7.2mm, altura 8.5mm)
spindle_pin = Manifold.cylinder(height=8.5, radius_low=3.6, radius_high=3.4, circular_segments=32).translate([0.0, 0.0, 4.5])

# 3.4 Cubo Inferior de Acoplamento do Motor TT (Diâmetro 12mm x 9mm de profundidade)
motor_hub = Manifold.cylinder(height=8.5, radius_low=6.0, radius_high=6.0, circular_segments=32).translate([0.0, 0.0, -8.5])

# 3.5 Furo Chaveado do Motor Amarelo TT:
# Cilindro 5.5mm interceptado por cubo de 3.8mm (cria o duplo chanfro exato do eixo TT)
tt_cyl = Manifold.cylinder(height=14.0, radius_low=2.75, radius_high=2.75, circular_segments=32)
tt_box = Manifold.cube([10.0, 3.8, 16.0], center=True)
tt_shaft_bore = (tt_cyl ^ tt_box).translate([0.0, 0.0, -9.0])

platter_final = (platter_base + platter_rim + spindle_pin + motor_hub) - tt_shaft_bore

save_stl(platter_final, "Vitrola_Prato_Giratorio.stl")


# ==============================================================================
# 4. BOTÃO KNOB RETRÔ RECARTILHADO PARA O ENCODER ROTATIVO
# ==============================================================================
print("\n[4/4] Gerando Vitrola_Knob_Volume.stl...")

# Diâmetro externo 22mm x 12mm de altura
knob_body = Manifold.cylinder(height=11.5, radius_low=11.0, radius_high=10.5, circular_segments=48)

# Ranhuras verticais recartilhadas para pegada tátil suave (20 ranhuras ao redor)
ribs_cutters = []
for r in range(20):
    ang = np.radians(r * 18.0)
    rx = 11.0 * np.cos(ang)
    ry = 11.0 * np.sin(ang)
    rib = Manifold.cylinder(height=12.0, radius_low=0.75, radius_high=0.75, circular_segments=16).translate([rx, ry, 0.0])
    knob_body = knob_body - rib

# Indicador visual no topo (entalhe elegante de volume)
indicator = Manifold.cube([1.4, 6.0, 1.2], center=True).translate([0.0, 6.5, 11.0])
knob_body = knob_body - indicator

# Furo do eixo do encoder: Eixo D de 6.0mm (chanfrado em 4.6mm) com profundidade de 9mm
enc_cyl = Manifold.cylinder(height=10.0, radius_low=3.05, radius_high=3.05, circular_segments=32)
enc_flat = Manifold.cube([10.0, 4.7, 12.0], center=True).translate([0.0, -0.65, 0.0])
enc_shaft_hole = (enc_cyl ^ enc_flat).translate([0.0, 0.0, -0.5])

knob_final = knob_body - enc_shaft_hole

save_stl(knob_final, "Vitrola_Knob_Volume.stl")

print("\n======================================================================")
print("  TODOS OS 4 ARQUIVOS STL GERADOS COM SUCESSO!")
print("======================================================================")
