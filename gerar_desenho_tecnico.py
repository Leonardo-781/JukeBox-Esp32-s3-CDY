import matplotlib.pyplot as plt
import matplotlib.patches as patches
import numpy as np
from pathlib import Path

OUT_DIR = Path("C:/Users/Leonardo/.gemini/antigravity/brain/446c37be-28ef-4571-a5bf-f3ec1809aac7")
OUT_IMG = OUT_DIR / "desenho_tecnico_vitrola.png"

# Configura figura estilo Blueprint de Engenharia
fig = plt.figure(figsize=(20, 14), dpi=180)
fig.patch.set_facecolor('#0b192c') # Azul marinho blueprint profundo

# Estilo de linhas e textos
C_LINE = '#00ffff'      # Ciano brilhante para contorno da carcaça
C_DASH = '#1e90ff'      # Azul claro para linhas ocultas/eixos
C_DIM  = '#ffffff'      # Branco para cotas e medidas
C_TXT  = '#e0e6ed'      # Texto padrão
C_NOTE = '#ffd166'      # Amarelo para anotações e componentes
C_BG   = '#0e223d'      # Fundo dos quadros técnicos

def setup_ax(ax, title, xlim, ylim):
    ax.set_facecolor(C_BG)
    ax.set_title(title, color='#00ffff', fontsize=14, fontweight='bold', pad=12, loc='left')
    ax.set_xlim(xlim)
    ax.set_ylim(ylim)
    ax.set_aspect('equal')
    # Grade suave de prancheta
    ax.grid(True, linestyle=':', color='#1e3d59', alpha=0.6, linewidth=0.7)
    ax.tick_params(colors='#88a2b8', labelsize=9)
    for spine in ax.spines.values():
        spine.set_color('#1e3d59')
        spine.set_linewidth(1.5)

# Helper para desenhar cota com setas e valor
def draw_dim(ax, x1, y1, x2, y2, text, offset=0, orientation='h', text_offset=2):
    if orientation == 'h':
        # Cota horizontal
        y = y1 + offset
        ax.plot([x1, x2], [y, y], color=C_DIM, linewidth=1.0)
        ax.plot([x1, x1], [y1, y + np.sign(offset)*1.5], color=C_DIM, linewidth=0.6, linestyle='--')
        ax.plot([x2, x2], [y2, y + np.sign(offset)*1.5], color=C_DIM, linewidth=0.6, linestyle='--')
        # Pontas de seta
        ax.annotate('', xy=(x1, y), xytext=(x1 + 4, y), arrowprops=dict(arrowstyle='->', color=C_DIM, lw=1))
        ax.annotate('', xy=(x2, y), xytext=(x2 - 4, y), arrowprops=dict(arrowstyle='->', color=C_DIM, lw=1))
        ax.text((x1 + x2)/2.0, y + text_offset, text, color=C_DIM, fontsize=9, fontweight='bold', ha='center', va='bottom')
    elif orientation == 'v':
        # Cota vertical
        x = x1 + offset
        ax.plot([x, x], [y1, y2], color=C_DIM, linewidth=1.0)
        ax.plot([x1, x + np.sign(offset)*1.5], [y1, y1], color=C_DIM, linewidth=0.6, linestyle='--')
        ax.plot([x2, x + np.sign(offset)*1.5], [y2, y2], color=C_DIM, linewidth=0.6, linestyle='--')
        ax.annotate('', xy=(x, y1), xytext=(x, y1 + 4), arrowprops=dict(arrowstyle='->', color=C_DIM, lw=1))
        ax.annotate('', xy=(x, y2), xytext=(x, y2 - 4), arrowprops=dict(arrowstyle='->', color=C_DIM, lw=1))
        ax.text(x + text_offset, (y1 + y2)/2.0, text, color=C_DIM, fontsize=9, fontweight='bold', ha='left', va='center')

# ==============================================================================
# QUADRO 1: VISTA LATERAL DIREITA (PERFIL Y-Z) - O ESBOÇO ORIGINAL
# ==============================================================================
ax1 = fig.add_subplot(2, 2, 1)
setup_ax(ax1, "VISTA LATERAL (PERFIL Y-Z) — GEOMETRIA EM DEGRAU", [-20, 180], [-15, 125])

# Contorno Externo Principal (Y: 0 a 150, Z: 0 a 106)
y_profile = [0, 150, 150, 56, 14, 0, 0]
z_profile = [0, 0, 46, 46, 106, 106, 0]
ax1.plot(y_profile, z_profile, color=C_LINE, linewidth=2.5, label='Contorno Externo')

# Espessura de parede interna (Tracejado)
y_in = [2.8, 147.2, 147.2, 54.0, 16.0, 2.8, 2.8]
z_in = [2.4, 2.4, 43.2, 43.2, 103.2, 103.2, 2.4]
ax1.plot(y_in, z_in, color=C_DASH, linewidth=1.0, linestyle=':', alpha=0.7)

# Prato de Vinil em corte sobre o degrau
# Prato: Y de 103-41=62 a 103+41=144, Z de 44 a 48.5 mm
rect_prato = patches.Rectangle((62, 44), 82, 4.5, linewidth=1.5, edgecolor='#2a9d8f', facecolor='#2a9d8f', alpha=0.5)
ax1.add_patch(rect_prato)
# Pino central
rect_pino = patches.Rectangle((103-3.6, 48.5), 7.2, 8.5, linewidth=1.2, edgecolor='#2a9d8f', facecolor='#2a9d8f')
ax1.add_patch(rect_pino)

# Motor Amarelo TT embaixo do deck
rect_motor = patches.Rectangle((103-12, 18), 44, 22, linewidth=1.2, edgecolor='#e76f51', facecolor='#e76f51', alpha=0.4, linestyle='--')
ax1.add_patch(rect_motor)
ax1.text(125, 29, "MOTOR TT", color='#e76f51', fontsize=8, fontweight='bold', ha='center', va='center')

# Tela TFT 2.8" instalada na rampa inclinada
# Rampa vai de (14, 106) até (56, 46)
ax1.plot([23, 47], [93, 58], color='#ffd166', linewidth=4.0, alpha=0.8, label='Display TFT 2.8"')

# Chamadas e Cotas Laterais
draw_dim(ax1, 0, -8, 150, -8, "150.0 mm (Profundidade Total)", offset=0, orientation='h', text_offset=-5)
draw_dim(ax1, 56, 58, 150, 58, "94.0 mm (Deck do Vinil)", offset=0, orientation='h', text_offset=2)
draw_dim(ax1, 0, 112, 14, 112, "14.0", offset=0, orientation='h', text_offset=2)
draw_dim(ax1, -10, 0, -10, 106, "106.0 mm", offset=0, orientation='v', text_offset=-9)
draw_dim(ax1, 160, 0, 160, 46, "46.0 mm", offset=0, orientation='v', text_offset=3)

# Arco do ângulo de 55 graus
arc_ang = patches.Arc((56, 46), 25, 25, angle=0, theta1=125, theta2=180, color='#ffd166', linewidth=1.5)
ax1.add_patch(arc_ang)
ax1.text(40, 53, "55°", color='#ffd166', fontsize=10, fontweight='bold')

# Anotações
ax1.annotate('TELA TFT 2.8"\n(Inclinada a 55°)', xy=(35, 75.5), xytext=(65, 95),
             arrowprops=dict(facecolor=C_NOTE, edgecolor='none', shrink=0.08, width=1.5, headwidth=6),
             color=C_NOTE, fontsize=9, fontweight='bold')
ax1.annotate('DISCO / PRATO (Ø82mm)', xy=(103, 50), xytext=(108, 76),
             arrowprops=dict(facecolor='#2a9d8f', edgecolor='none', shrink=0.08, width=1.5, headwidth=6),
             color='#2a9d8f', fontsize=9, fontweight='bold')
ax1.annotate('ALTO-FALANTE I2S\n(Painel Frontal)', xy=(150, 24.5), xytext=(120, 5),
             arrowprops=dict(facecolor='#00ffff', edgecolor='none', shrink=0.08, width=1.5, headwidth=6),
             color='#00ffff', fontsize=8, fontweight='bold')
ax1.annotate('USB-C + CHAVE\n(Painel Traseiro)', xy=(0, 10), xytext=(-18, 35),
             arrowprops=dict(facecolor='#00ffff', edgecolor='none', shrink=0.08, width=1.5, headwidth=6),
             color='#00ffff', fontsize=8, fontweight='bold')

# ==============================================================================
# QUADRO 2: VISTA SUPERIOR (PLANTA X-Y)
# ==============================================================================
ax2 = fig.add_subplot(2, 2, 2)
setup_ax(ax2, "VISTA SUPERIOR (PLANTA X-Y) — DISPOSIÇÃO DO DECK E TELA", [-75, 75], [-15, 175])

# Contorno Externo (X: -63 a +63, Y: 0 a 150)
rect_top = patches.Rectangle((-63, 0), 126, 150, linewidth=2.5, edgecolor=C_LINE, facecolor='none')
ax2.add_patch(rect_top)

# Linha de divisão: topo plano (Y=14) e início do deck (Y=56)
ax2.plot([-63, 63], [14, 14], color=C_DASH, linewidth=1.2, linestyle='--')
ax2.plot([-63, 63], [56, 56], color=C_LINE, linewidth=2.0)

# Abertura da Tela na Rampa (X: -30 a +30, Y centralizada na rampa ~35)
rect_screen = patches.Rectangle((-30, 22), 60, 26, linewidth=1.8, edgecolor='#ffd166', facecolor='#ffd166', alpha=0.3)
ax2.add_patch(rect_screen)
ax2.text(0, 35, "JANELA TELA 2.8\"\n(60 × 44 mm)", color='#ffd166', fontsize=8, fontweight='bold', ha='center', va='center')

# Rebaixo do Prato de Vinil (Ø86mm) e Prato (Ø82mm)
circle_well = patches.Circle((0, 103), 43, linewidth=1.2, edgecolor='#1e90ff', facecolor='none', linestyle=':')
circle_platter = patches.Circle((0, 103), 41, linewidth=2.0, edgecolor='#2a9d8f', facecolor='#2a9d8f', alpha=0.35)
circle_spindle = patches.Circle((0, 103), 3.6, linewidth=1.5, edgecolor='#2a9d8f', facecolor='#2a9d8f')
ax2.add_patch(circle_well)
ax2.add_patch(circle_platter)
ax2.add_patch(circle_spindle)
ax2.text(0, 103, "EIXO Ø7.2", color='#ffffff', fontsize=7, fontweight='bold', ha='center', va='center')

# Trilhos RFID RC522 sob o deck (Tracejado)
rect_rfid = patches.Rectangle((-20, 73), 40, 60, linewidth=1.2, edgecolor='#ff5964', facecolor='none', linestyle='--', alpha=0.8)
ax2.add_patch(rect_rfid)
ax2.text(0, 78, "RFID RC522 (SOB O DECK)", color='#ff5964', fontsize=7, fontweight='bold', ha='center', va='center')

# Cotas
draw_dim(ax2, -63, -8, 63, -8, "126.0 mm (Largura Total)", offset=0, orientation='h', text_offset=-5)
draw_dim(ax2, 68, 0, 68, 150, "150.0 mm", offset=0, orientation='v', text_offset=3)
draw_dim(ax2, -41, 156, 41, 156, "Ø82.0 mm (Prato)", offset=0, orientation='h', text_offset=2)

# ==============================================================================
# QUADRO 3: VISTA FRONTAL (PAINEL DO USUÁRIO X-Z)
# ==============================================================================
ax3 = fig.add_subplot(2, 2, 3)
setup_ax(ax3, "VISTA FRONTAL (PAINEL DO USUÁRIO X-Z) — FALANTE E ENCODER", [-75, 75], [-15, 125])

# Contorno Frontal Stepped (Base frontal de Z=0 a 46, depois sobe a rampa até Z=106)
# Fachada do degrau frontal: X de -63 a +63, Z de 0 a 46
rect_front = patches.Rectangle((-63, 0), 126, 46, linewidth=2.5, edgecolor=C_LINE, facecolor='#0e223d')
ax3.add_patch(rect_front)

# Rampa inclinada da tela vista de frente (vai de Z=46 até Z=106)
x_roof = [-63, 63, 63, -63, -63]
z_roof = [46, 46, 106, 106, 46]
ax3.plot(x_roof, z_roof, color=C_LINE, linewidth=1.8, linestyle='--')

# Janela da tela vista de frente
rect_scr_front = patches.Rectangle((-30, 56), 60, 38, linewidth=1.5, edgecolor='#ffd166', facecolor='#ffd166', alpha=0.35)
ax3.add_patch(rect_scr_front)
ax3.text(0, 75, "MOLDURA TELA 2.8\"\n(ÁREA VISÍVEL)", color='#ffd166', fontsize=8, fontweight='bold', ha='center', va='center')

# Grade Acústica do Alto-falante (Centralizado à esquerda: X = -14 mm)
for i in range(5):
    z_slot = 15.0 + i * 5.2 - 1.2
    slot = patches.Rectangle((-14 - 24, z_slot), 48, 2.4, linewidth=1.0, edgecolor='#00ffff', facecolor='#00ffff', alpha=0.6)
    ax3.add_patch(slot)
ax3.text(-14, 25.4, "GRADE DO ALTO-FALANTE\n(MÓDULO I2S 66×30mm)", color='#ffffff', fontsize=7, fontweight='bold', ha='center', va='center')

# Furo e Knob do Encoder Rotativo (Lado direito: X = +43 mm, Z = 24.5 mm)
circle_knob_base = patches.Circle((43, 24.5), 11, linewidth=1.8, edgecolor='#e76f51', facecolor='#e76f51', alpha=0.5)
circle_knob_core = patches.Circle((43, 24.5), 3.6, linewidth=1.2, edgecolor='#ffffff', facecolor='#ffffff')
ax3.add_patch(circle_knob_base)
ax3.add_patch(circle_knob_core)
# Marcador do Knob
ax3.plot([43, 43], [24.5, 34.5], color='#ffffff', linewidth=2.0)
ax3.text(43, 8, "KNOB ENCODER\nVOLUME / CLICK", color='#e76f51', fontsize=7.5, fontweight='bold', ha='center', va='center')

# Cotas
draw_dim(ax3, -63, -8, 63, -8, "126.0 mm", offset=0, orientation='h', text_offset=-5)
draw_dim(ax3, 68, 0, 68, 46, "46.0 mm", offset=0, orientation='v', text_offset=3)
draw_dim(ax3, -70, 0, -70, 106, "106.0 mm", offset=0, orientation='v', text_offset=-8)

# ==============================================================================
# QUADRO 4: VISTA TRASEIRA & TABELA TÉCNICA (X-Z TRASEIRO + DADOS)
# ==============================================================================
ax4 = fig.add_subplot(2, 2, 4)
setup_ax(ax4, "VISTA TRASEIRA (PORTAS) & TABELA GERAL DE COMPONENTES", [-75, 75], [-15, 125])

# Fachada Traseira (X: -63 a +63, Z: 0 a 106)
rect_rear = patches.Rectangle((-63, 0), 126, 106, linewidth=2.5, edgecolor=C_LINE, facecolor='none')
ax4.add_patch(rect_rear)

# Abertura USB-C no lado esquerdo da traseira (X = -20 mm, Z = 6.5 mm)
rect_usb = patches.Rectangle((-27, 2.25), 14, 8.5, linewidth=1.5, edgecolor='#00ffff', facecolor='#00ffff', alpha=0.5)
ax4.add_patch(rect_usb)
ax4.text(-20, 6.5, "USB-C", color='#000000', fontsize=7.5, fontweight='bold', ha='center', va='center')

# Abertura Chave Gangorra KCD11 no lado direito da traseira (X = +20 mm, Z = 13 mm)
rect_sw = patches.Rectangle((13.25, 8.4), 13.5, 9.2, linewidth=1.5, edgecolor='#ffd166', facecolor='#ffd166', alpha=0.5)
ax4.add_patch(rect_sw)
ax4.text(20, 13.0, "CHAVE ON/OFF\n(KCD11)", color='#000000', fontsize=6.5, fontweight='bold', ha='center', va='center')

# Bloco Técnico de Resumo do Projeto (Title Block de Engenharia)
tbox = patches.Rectangle((-60, 32), 120, 68, linewidth=1.2, edgecolor='#1e90ff', facecolor='#0b192c', alpha=0.9)
ax4.add_patch(tbox)

info_text = (
    "PROJETO: CONSOLE VITROLA JUKEBOX ESP32 RETRÔ\n"
    "AUTOR: Leonardo | SISTEMA: Antigravity 2.0\n"
    "--------------------------------------------------------------------\n"
    "• DIMENSÕES TOTAIS: 126 mm (L) × 150 mm (P) × 106 mm (A)\n"
    "• ESPESSURA DE PAREDE: 2.8 mm (Máxima rigidez acústica)\n"
    "• VOLUME DE IMPRESSÃO: Ocupa 39% da mesa Ender 3 v2 Neo\n"
    "• DISPLAY: TFT 2.8\" SPI (ILI9341 320×240) inclinado a 55°\n"
    "• PRATO DE VINIL: Ø82 mm com duplo chanfro Motor TT (5.4×3.7mm)\n"
    "• LEITOR RFID: RC522 13.56MHz em trilho deslizante sob o deck\n"
    "• ÁUDIO: Falante I2S (66×30×17mm) + Amplificador MAX98357A\n"
    "• CONTROLE: Encoder Rotativo (Volume analógico + Play/Pause)\n"
    "• ENERGIA: Conector USB-C + Chave Gangorra KCD11 10×15mm\n"
    "• FECHAMENTO: Tampa da base com 4 parafusos M3 escareados"
)
ax4.text(-56, 66, info_text, color='#e0e6ed', fontsize=8, family='monospace', va='center')

plt.tight_layout()
plt.savefig(str(OUT_IMG), dpi=180, facecolor=fig.get_facecolor(), edgecolor='none')
print(f"Desenho Técnico Blueprint salvo com sucesso em: {OUT_IMG}")
