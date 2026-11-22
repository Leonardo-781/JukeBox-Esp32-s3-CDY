import trimesh
import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection
from matplotlib.colors import LightSource
from pathlib import Path

STL_DIR = Path("c:/Users/Leonardo/OneDrive/Documentos/ideias/Jukebox/3D")
OUT_IMG = Path("C:/Users/Leonardo/.gemini/antigravity/brain/446c37be-28ef-4571-a5bf-f3ec1809aac7/preview_vitrola_3d.png")

mesh_console = trimesh.load(str(STL_DIR / "Vitrola_Console_Gabinete.stl"))
mesh_bottom  = trimesh.load(str(STL_DIR / "Vitrola_Tampa_Fundo.stl"))
mesh_platter = trimesh.load(str(STL_DIR / "Vitrola_Prato_Giratorio.stl"))
mesh_knob    = trimesh.load(str(STL_DIR / "Vitrola_Knob_Volume.stl"))

# Posiciona prato no deck (X=0, Y=103.0, Z=44.0)
mesh_platter_assembled = mesh_platter.copy()
mesh_platter_assembled.apply_translation([0.0, 103.0, 44.0])

# Posiciona knob no painel frontal (X=43.0, Y=150.0, Z=24.5)
mesh_knob_assembled = mesh_knob.copy()
# O knob foi gerado ao longo de Z, rotacionamos para apontar na direção Y
mesh_knob_assembled.apply_transform(trimesh.transformations.rotation_matrix(np.radians(-90), [1, 0, 0]))
mesh_knob_assembled.apply_translation([43.0, 150.0, 24.5])

fig = plt.figure(figsize=(16, 12), dpi=160)
fig.patch.set_facecolor('#141419')

ls = LightSource(azdeg=215, altdeg=40)

def plot_shaded_mesh(ax, mesh, base_color='#e59846', alpha=1.0, step=1):
    faces = mesh.faces[::step]
    triangles = mesh.vertices[faces]
    
    v0 = triangles[:, 0]
    v1 = triangles[:, 1]
    v2 = triangles[:, 2]
    normals = np.cross(v1 - v0, v2 - v0)
    norm_lens = np.linalg.norm(normals, axis=1, keepdims=True)
    norm_lens[norm_lens == 0] = 1.0
    normals = normals / norm_lens
    
    intensity = ls.shade_normals(normals, fraction=0.7)
    c_rgb = np.array(plt.cm.colors.to_rgb(base_color))
    final_colors = np.outer(intensity, c_rgb)
    if alpha < 1.0:
        final_colors = np.column_stack([final_colors, np.full(len(final_colors), alpha)])
    
    poly = Poly3DCollection(triangles, facecolors=final_colors, edgecolors='none', linewidths=0)
    ax.add_collection3d(poly)

# 1. Vista Frontal 3D (Console Montado com Knob e Grade)
ax1 = fig.add_subplot(2, 2, 1, projection='3d')
ax1.set_facecolor('#141419')
plot_shaded_mesh(ax1, mesh_console, base_color='#e09f3e')
plot_shaded_mesh(ax1, mesh_platter_assembled, base_color='#2a9d8f')
plot_shaded_mesh(ax1, mesh_knob_assembled, base_color='#e76f51')
ax1.set_title("1. Vista Frontal 3D (Tela 55° + Prato Motor TT + Knob + Falante)", color='#ffffff', fontsize=12, fontweight='bold', pad=10)
ax1.view_init(elev=26, azim=135)
ax1.set_xlim([-70, 70])
ax1.set_ylim([0, 160])
ax1.set_zlim([0, 115])
ax1.set_axis_off()

# 2. Vista Traseira 3D (USB-C + Chave Liga/Desliga)
ax2 = fig.add_subplot(2, 2, 2, projection='3d')
ax2.set_facecolor('#141419')
plot_shaded_mesh(ax2, mesh_console, base_color='#e09f3e')
plot_shaded_mesh(ax2, mesh_platter_assembled, base_color='#2a9d8f')
ax2.set_title("2. Vista Traseira (Porta USB-C à Esq + Chave Liga/Desliga à Dir)", color='#ffffff', fontsize=12, fontweight='bold', pad=10)
ax2.view_init(elev=24, azim=-45)
ax2.set_xlim([-70, 70])
ax2.set_ylim([0, 160])
ax2.set_zlim([0, 115])
ax2.set_axis_off()

# 3. Vista Inferior Interna (Berço do Motor TT, RFID, Falante, Tela)
ax3 = fig.add_subplot(2, 2, 3, projection='3d')
ax3.set_facecolor('#141419')
plot_shaded_mesh(ax3, mesh_console, base_color='#c85a54')
ax3.set_title("3. Vista Inferior Interna (Berço Motor TT + Trilhos RFID + Suporte Tela)", color='#ffffff', fontsize=12, fontweight='bold', pad=10)
ax3.view_init(elev=-48, azim=-55)
ax3.set_xlim([-70, 70])
ax3.set_ylim([0, 160])
ax3.set_zlim([0, 115])
ax3.set_axis_off()

# 4. Vista Explodida (Gabinete + Tampa Fundo ESP32/DAC + Prato + Knob)
ax4 = fig.add_subplot(2, 2, 4, projection='3d')
ax4.set_facecolor('#141419')
plot_shaded_mesh(ax4, mesh_console, base_color='#e09f3e')

mesh_p_exp = mesh_platter.copy()
mesh_p_exp.apply_translation([0.0, 103.0, 70.0])
plot_shaded_mesh(ax4, mesh_p_exp, base_color='#2a9d8f')

mesh_k_exp = mesh_knob_assembled.copy()
mesh_k_exp.apply_translation([0.0, 25.0, 0.0])
plot_shaded_mesh(ax4, mesh_k_exp, base_color='#e76f51')

mesh_b_exp = mesh_bottom.copy()
mesh_b_exp.apply_translation([0.0, 0.0, -32.0])
plot_shaded_mesh(ax4, mesh_b_exp, base_color='#457b9d')

ax4.set_title("4. Vista Explodida Completa (Todos os Módulos)", color='#ffffff', fontsize=12, fontweight='bold', pad=10)
ax4.view_init(elev=25, azim=135)
ax4.set_xlim([-70, 70])
ax4.set_ylim([0, 185])
ax4.set_zlim([-45, 115])
ax4.set_axis_off()

plt.tight_layout()
plt.savefig(str(OUT_IMG), dpi=160, facecolor=fig.get_facecolor(), edgecolor='none')
print("Render com iluminação e sombreamento atualizado com sucesso!")
