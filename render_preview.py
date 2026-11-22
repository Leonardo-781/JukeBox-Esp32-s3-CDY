import trimesh
import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
from pathlib import Path

STL_DIR = Path("c:/Users/Leonardo/OneDrive/Documentos/ideias/Jukebox/3D")
OUT_IMG = Path("C:/Users/Leonardo/.gemini/antigravity/brain/446c37be-28ef-4571-a5bf-f3ec1809aac7/preview_vitrola_3d.png")

print("Carregando arquivos STL...")
mesh_console = trimesh.load(str(STL_DIR / "Vitrola_Console_Gabinete.stl"))
mesh_bottom  = trimesh.load(str(STL_DIR / "Vitrola_Tampa_Fundo.stl"))
mesh_platter = trimesh.load(str(STL_DIR / "Vitrola_Prato_Giratorio.stl"))

# Posiciona o prato no lugar exato do rebaixo
# Rebaixo está em X=0, Y=100.5, Z=44.0
mesh_platter_assembled = mesh_platter.copy()
mesh_platter_assembled.apply_translation([0.0, 100.5, 42.0])

fig = plt.figure(figsize=(16, 12), dpi=150)
fig.patch.set_facecolor('#1e1e24')

def plot_mesh_to_ax(ax, mesh, facecolor='#d9822b', edgecolor='#333333', alpha=0.9, lw=0.1):
    # Simplifica para plotagem rápida se tiver muitas faces
    if len(mesh.faces) > 2000:
        sub_faces = mesh.faces[::2]
    else:
        sub_faces = mesh.faces
    triangles = mesh.vertices[sub_faces]
    poly = Poly3DCollection(triangles, facecolors=facecolor, edgecolors=edgecolor, linewidths=lw, alpha=alpha)
    ax.add_collection3d(poly)

# ------------------------------------------------------------------------------
# 1. VISTA ISOMÉTRICA (PERSPECTIVA EXTERNA)
# ------------------------------------------------------------------------------
ax1 = fig.add_subplot(2, 2, 1, projection='3d')
ax1.set_facecolor('#1e1e24')
plot_mesh_to_ax(ax1, mesh_console, facecolor='#e09f3e', alpha=0.95)
plot_mesh_to_ax(ax1, mesh_platter_assembled, facecolor='#335c67', alpha=1.0)
ax1.set_title("1. Vista Isométrica (Console Montado)", color='#ffffff', fontsize=13, fontweight='bold', pad=10)
ax1.view_init(elev=32, azim=-55)
ax1.set_xlim([-70, 70])
ax1.set_ylim([0, 150])
ax1.set_zlim([0, 120])
ax1.set_axis_off()

# ------------------------------------------------------------------------------
# 2. PERFIL LATERAL (Y-Z) - COMPARATIVO EXATO DO ESBOÇO
# ------------------------------------------------------------------------------
ax2 = fig.add_subplot(2, 2, 2, projection='3d')
ax2.set_facecolor('#1e1e24')
plot_mesh_to_ax(ax2, mesh_console, facecolor='#e09f3e', alpha=0.9)
plot_mesh_to_ax(ax2, mesh_platter_assembled, facecolor='#335c67', alpha=1.0)
ax2.set_title("2. Perfil Lateral (Comparativo do Esboço)", color='#ffffff', fontsize=13, fontweight='bold', pad=10)
ax2.view_init(elev=0, azim=0) # Vista de lado perfeita
ax2.set_xlim([-70, 70])
ax2.set_ylim([0, 150])
ax2.set_zlim([0, 120])
ax2.set_axis_off()

# ------------------------------------------------------------------------------
# 3. VISTA INFERIOR / INTERNA (MONTAGEM DOS COMPONENTES)
# ------------------------------------------------------------------------------
ax3 = fig.add_subplot(2, 2, 3, projection='3d')
ax3.set_facecolor('#1e1e24')
# Vendo por baixo para inspecionar os suportes internos
plot_mesh_to_ax(ax3, mesh_console, facecolor='#9e2a2b', alpha=0.85)
ax3.set_title("3. Vista Inferior Interna (Torres Tela, RFID, Motor, Falante)", color='#ffffff', fontsize=13, fontweight='bold', pad=10)
ax3.view_init(elev=-48, azim=-55)
ax3.set_xlim([-70, 70])
ax3.set_ylim([0, 150])
ax3.set_zlim([0, 120])
ax3.set_axis_off()

# ------------------------------------------------------------------------------
# 4. EXPLODIDA (GABINETE + TAMPA INFERIOR COM ESP32 + PRATO)
# ------------------------------------------------------------------------------
ax4 = fig.add_subplot(2, 2, 4, projection='3d')
ax4.set_facecolor('#1e1e24')
# Gabinete no meio
plot_mesh_to_ax(ax4, mesh_console, facecolor='#e09f3e', alpha=0.85)
# Prato elevado no topo
mesh_p_exp = mesh_platter.copy()
mesh_p_exp.apply_translation([0.0, 100.5, 68.0])
plot_mesh_to_ax(ax4, mesh_p_exp, facecolor='#335c67', alpha=1.0)
# Tampa do fundo rebaixada
mesh_b_exp = mesh_bottom.copy()
mesh_b_exp.apply_translation([0.0, 0.0, -35.0])
plot_mesh_to_ax(ax4, mesh_b_exp, facecolor='#540b0e', alpha=0.95)

ax4.set_title("4. Montagem Explodida (Prato, Gabinete, Base ESP32)", color='#ffffff', fontsize=13, fontweight='bold', pad=10)
ax4.view_init(elev=28, azim=-40)
ax4.set_xlim([-70, 70])
ax4.set_ylim([0, 150])
ax4.set_zlim([-45, 120])
ax4.set_axis_off()

plt.tight_layout()
plt.savefig(str(OUT_IMG), dpi=150, facecolor=fig.get_facecolor(), edgecolor='none')
print(f"Render salvo em: {OUT_IMG}")
