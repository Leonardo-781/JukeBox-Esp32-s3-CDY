import trimesh
import numpy as np
from pathlib import Path
from shapely.geometry import Polygon

OUT_DIR = Path("c:/Users/Leonardo/OneDrive/Documentos/ideias/Jukebox/3D")
OUT_DIR.mkdir(parents=True, exist_ok=True)

print("==========================================================")
print("   GERADOR 3D - VITROLA CONSOLE JUKEBOX (PERFIL EM DEGRAU)")
print("==========================================================")

# ==============================================================================
# DIMENSÕES PRINCIPAIS (COLETADAS DO HARDWARE)
# ==============================================================================
WIDTH   = 110.0   # Largura externa (X: -55 a +55 mm)
DEPTH   = 130.0   # Profundidade total (Y: 0 a 130 mm)
H_REAR  = 108.0   # Altura traseira máxima
H_FRONT = 45.0    # Altura do degrau frontal (onde fica o vinil e o alto-falante)
WALL    = 2.6     # Espessura das paredes externas

# Y_SHELF_START = onde termina a rampa da tela e começa o prato plano
Y_RAMP_TOP = 18.0
Y_SHELF_START = 64.0

# ------------------------------------------------------------------------------
# 1. PERFIL 2D LATERAL EXTERNO (Plano Y-Z)
# ------------------------------------------------------------------------------
# Formato exato do esboço do Leonardo:
# (Y=0, Z=0) -> Base traseira
# (Y=DEPTH, Z=0) -> Base frontal
# (Y=DEPTH, Z=H_FRONT) -> Topo da frente (alto-falante embaixo)
# (Y=Y_SHELF_START, Z=H_FRONT) -> Degrau horizontal (prato de vinil)
# (Y=Y_RAMP_TOP, Z=H_REAR) -> Topo da rampa inclinada (tela)
# (Y=0, Z=H_REAR) -> Topo traseiro
poly_outer_pts = [
    [0.0, 0.0],
    [DEPTH, 0.0],
    [DEPTH, H_FRONT],
    [Y_SHELF_START, H_FRONT],
    [Y_RAMP_TOP, H_REAR],
    [0.0, H_REAR]
]
poly_outer = Polygon(poly_outer_pts)

# Extrusão do corpo sólido ao longo do eixo X (-WIDTH/2 até +WIDTH/2)
body_outer = trimesh.creation.extrude_polygon(poly_outer, height=WIDTH)
# O extrude projeta em Z, vamos girar para orientar corretamente (X=largura, Y=profundidade, Z=altura)
# Extrusão padrão projeta os pontos (X, Y) ao longo de Z.
# Aqui nossos pontos foram definidos como (Y, Z), então precisamos mapear:
# Ponto_2D(Y, Z) -> Ponto_3D(X, Y, Z)

# Vamos criar a malha diretamente definindo a extrusão em X:
def create_prism_from_yz_poly(poly_pts, x_min, x_max):
    poly2d = Polygon(poly_pts)
    mesh = trimesh.creation.extrude_polygon(poly2d, height=x_max - x_min)
    # mesh vem com X=Ponto_Y, Y=Ponto_Z, Z=extrusao
    # Rotacionamos para: X=extrusao, Y=Ponto_Y, Z=Ponto_Z
    # Matriz de transformação:
    # X_novo = Z_velho + x_min
    # Y_novo = X_velho
    # Z_novo = Y_velho
    T = np.array([
        [0, 0, 1, x_min],
        [1, 0, 0, 0],
        [0, 1, 0, 0],
        [0, 0, 0, 1]
    ], dtype=float)
    mesh.apply_transform(T)
    return mesh

body_solid = create_prism_from_yz_poly(poly_outer_pts, -WIDTH / 2.0, WIDTH / 2.0)
print(f"[1/4] Corpo externo prismático gerado ({len(body_solid.faces)} faces).")

# ------------------------------------------------------------------------------
# 2. CAVIDADE INTERNA (OCA POR BAIXO PARA ACOMODAR TODOS OS CIRCUITOS)
# ------------------------------------------------------------------------------
poly_inner_pts = [
    [WALL, 0.0], # Aberto no fundo
    [DEPTH - WALL, 0.0],
    [DEPTH - WALL, H_FRONT - WALL],
    [Y_SHELF_START - WALL * 0.7, H_FRONT - WALL],
    [Y_RAMP_TOP + WALL * 0.7, H_REAR - WALL],
    [WALL, H_REAR - WALL]
]
cavity = create_prism_from_yz_poly(poly_inner_pts, -WIDTH / 2.0 + WALL, WIDTH / 2.0 - WALL)

# Casca oca da vitrola
console_shell = body_solid.difference(cavity)
print(f"[2/4] Cavidade interna oca criada com paredes de {WALL}mm.")

# ------------------------------------------------------------------------------
# 3. CORTES E ABERTURAS FÍSICAS NO GABINETE
# ------------------------------------------------------------------------------

# 3.1 Rebaixo do Prato de Vinil no Degrau Horizontal (Z = H_FRONT)
# Centro do prato: X = 0, Y = (Y_SHELF_START + DEPTH) / 2 = 97 mm
platter_cx = 0.0
platter_cy = (Y_SHELF_START + DEPTH) / 2.0
platter_well = trimesh.creation.cylinder(radius=43.0, height=3.0, sections=64)
platter_well.apply_translation([platter_cx, platter_cy, H_FRONT - 1.5])

# Furo do eixo do motor (atravessa para a cavidade interna)
spindle_hole = trimesh.creation.cylinder(radius=5.5, height=12.0, sections=32)
spindle_hole.apply_translation([platter_cx, platter_cy, H_FRONT])

# 3.2 Janela da Tela TFT 2.8" na Rampa Inclinada
# A rampa vai de Y_RAMP_TOP(18, 108) até Y_SHELF_START(64, 45)
# Ângulo da rampa em relação ao plano horizontal:
dy = Y_SHELF_START - Y_RAMP_TOP # 46 mm
dz = H_REAR - H_FRONT           # 63 mm
angle_rad = np.arctan2(dz, dy)  # ~53.8 graus de inclinação
angle_deg = np.degrees(angle_rad)

# Centro da rampa
ramp_mid_y = (Y_RAMP_TOP + Y_SHELF_START) / 2.0 # 41 mm
ramp_mid_z = (H_REAR + H_FRONT) / 2.0           # 76.5 mm

# Abertura visível para a tela 2.8" (71mm x 51mm)
screen_cutter = trimesh.creation.box(extents=(72.0, 52.0, 25.0))
# Rotação em torno do eixo X para alinhar perfeitamente com a inclinação da rampa
rot_x = trimesh.transformations.rotation_matrix(np.radians(angle_deg - 90), [1, 0, 0])
screen_cutter.apply_transform(rot_x)
screen_cutter.apply_translation([0.0, ramp_mid_y, ramp_mid_z])

# 3.3 Grade do Alto-falante na Parede Frontal (Y = DEPTH)
# O alto-falante acústico I2S mede 66x30mm, fica na vertical frontal
speaker_slots = []
for i in range(5):
    z_slot = 15.0 + i * 5.5
    slot = trimesh.creation.box(extents=(56.0, 15.0, 2.5))
    slot.apply_translation([0.0, DEPTH, z_slot])
    speaker_slots.append(slot)

# 3.4 Conector de Energia USB-C na Base Traseira (Y = 0)
usbc_cut = trimesh.creation.box(extents=(13.0, 15.0, 7.5))
usbc_cut.apply_translation([0.0, 0.0, 6.0])

# 3.5 Pilares internos nos 4 cantos da base para parafusos M3 da tampa inferior
pillars = []
pillar_holes = []
for px in [-WIDTH / 2.0 + 8.0, WIDTH / 2.0 - 8.0]:
    for py in [8.0, DEPTH - 8.0]:
        h_pillar = H_FRONT if py > 60 else H_REAR - 10.0
        pillar = trimesh.creation.cylinder(radius=4.5, height=22.0, sections=24)
        pillar.apply_translation([px, py, 11.0])
        pillars.append(pillar)

        # Furo piloto M3 para rosquear
        p_hole = trimesh.creation.cylinder(radius=1.35, height=26.0, sections=24)
        p_hole.apply_translation([px, py, 11.0])
        pillar_holes.append(p_hole)

# Aplica cortes e adições
console = console_shell.difference([platter_well, spindle_hole, screen_cutter, usbc_cut])
console = console.difference(speaker_slots)
console = console.union(pillars)
console = console.difference(pillar_holes)

console_path = OUT_DIR / "Vitrola_Console_Gabinete.stl"
console.export(str(console_path))
print(f"[3/4] Gabinete Console gerado com sucesso: {console_path} ({len(console.faces)} faces)")

# ==============================================================================
# 4. TAMPA INFERIOR / BASE FECHADA
# ==============================================================================
# Tampa do fundo com furos escareados para parafusos M3 e pés de apoio
bottom_plate = trimesh.creation.box(extents=(WIDTH, DEPTH, 2.4))
bottom_plate.apply_translation([0.0, DEPTH / 2.0, 1.2])

plate_holes = []
for px in [-WIDTH / 2.0 + 8.0, WIDTH / 2.0 - 8.0]:
    for py in [8.0, DEPTH - 8.0]:
        h = trimesh.creation.cylinder(radius=1.7, height=10.0, sections=24) # Furo 3.4mm
        h.apply_translation([px, py, 1.2])
        c = trimesh.creation.cylinder(radius=3.2, height=1.5, sections=24) # Cabeça escareada
        c.apply_translation([px, py, 0.75])
        plate_holes.extend([h, c])

# Grade de ventilação no fundo
vent_slots = []
for i in range(8):
    y_v = 40.0 + i * 6.0
    v = trimesh.creation.box(extents=(45.0, 2.5, 8.0))
    v.apply_translation([0.0, y_v, 1.2])
    vent_slots.append(v)

bottom_plate = bottom_plate.difference(plate_holes)
bottom_plate = bottom_plate.difference(vent_slots)

bottom_path = OUT_DIR / "Vitrola_Tampa_Fundo.stl"
bottom_plate.export(str(bottom_path))
print(f"[4/4] Tampa inferior de fechamento gerada: {bottom_path} ({len(bottom_plate.faces)} faces)")

# ==============================================================================
# 5. PRATO GIRATÓRIO AJUSTADO AO DEGRAU
# ==============================================================================
platter_disc = trimesh.creation.cylinder(radius=40.0, height=4.0, sections=64)
platter_disc.apply_translation([0, 0, 2.0])

center_pin = trimesh.creation.cylinder(radius=3.6, height=9.0, sections=32)
center_pin.apply_translation([0, 0, 4.0 + 4.5])

motor_boss = trimesh.creation.cylinder(radius=5.5, height=5.5, sections=32)
motor_boss.apply_translation([0, 0, -2.75])

shaft_bore = trimesh.creation.cylinder(radius=1.1, height=10.0, sections=24)
shaft_bore.apply_translation([0, 0, -2.0])

platter_final = platter_disc.union([center_pin, motor_boss])
platter_final = platter_final.difference(shaft_bore)

platter_final_path = OUT_DIR / "Vitrola_Prato_Giratorio.stl"
platter_final.export(str(platter_final_path))
print(f" -> Prato giratório atualizado: {platter_final_path} ({len(platter_final.faces)} faces)")

print("\n==========================================================")
print(" MODELAGEM 3D DO CONSOLE FINALIZADA COM 100% DE SUCESSO!")
print("==========================================================")
