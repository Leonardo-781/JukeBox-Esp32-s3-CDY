#pragma once
#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
  <title>Vitrola Jukebox - Cadastro de Discos</title>
  <style>
    :root {
      --bg: #121212;
      --card: #1e1e1e;
      --card-alt: #282828;
      --green: #1DB954;
      --green-dark: #15883e;
      --amber: #f59e0b;
      --text: #ffffff;
      --muted: #a3a3a3;
      --border: #333333;
      --danger: #ef4444;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
    body { background: var(--bg); color: var(--text); padding: 14px; padding-bottom: 90px; }
    .header { text-align: center; margin-bottom: 16px; }
    .header h1 { font-size: 1.4rem; color: var(--green); display: flex; align-items: center; justify-content: center; gap: 8px; }
    .header p { font-size: 0.8rem; color: var(--muted); margin-top: 3px; }
    .card { background: var(--card); border: 1px solid var(--border); border-radius: 12px; padding: 16px; margin-bottom: 14px; box-shadow: 0 4px 14px rgba(0,0,0,0.4); }
    .card-title { font-size: 1.05rem; font-weight: 700; margin-bottom: 12px; display: flex; align-items: center; justify-content: space-between; }
    
    /* Tag Live Badge */
    .tag-badge { background: #181818; border: 2px dashed var(--border); border-radius: 10px; padding: 12px; text-align: center; margin-bottom: 14px; transition: all 0.3s; }
    .tag-badge.detected { background: rgba(29, 185, 84, 0.12); border-color: var(--green); border-style: solid; }
    .tag-badge .tag-label { font-size: 0.75rem; text-transform: uppercase; letter-spacing: 1px; color: var(--muted); }
    .tag-badge .tag-val { font-size: 1.3rem; font-weight: 700; color: var(--green); margin-top: 4px; font-family: monospace; }
    .tag-badge.empty .tag-val { color: var(--amber); font-size: 0.95rem; font-weight: 500; font-family: inherit; }

    /* Forms */
    label { display: block; font-size: 0.82rem; color: var(--muted); margin-bottom: 6px; font-weight: 600; }
    input, select { width: 100%; background: #262626; border: 1px solid #3e3e3e; border-radius: 8px; color: #fff; padding: 12px; font-size: 0.95rem; margin-bottom: 12px; outline: none; }
    input:focus, select:focus { border-color: var(--green); }
    .search-box { margin-bottom: 6px; font-size: 0.85rem; padding: 10px; }

    /* Buttons */
    .btn { width: 100%; background: var(--green); color: #000; font-weight: 700; font-size: 0.95rem; padding: 14px; border: none; border-radius: 8px; cursor: pointer; display: flex; align-items: center; justify-content: center; gap: 8px; transition: 0.2s; }
    .btn:active { background: var(--green-dark); transform: scale(0.98); }
    .btn-secondary { background: #333; color: #fff; font-size: 0.82rem; padding: 6px 12px; border-radius: 6px; border: none; cursor: pointer; }
    .btn-danger { background: rgba(239, 68, 68, 0.2); color: var(--danger); border: 1px solid var(--danger); font-size: 0.78rem; padding: 5px 10px; border-radius: 6px; cursor: pointer; }

    /* Discs List */
    .disc-list { max-height: 280px; overflow-y: auto; }
    .disc-item { display: flex; align-items: center; justify-content: space-between; padding: 10px; border-bottom: 1px solid var(--border); }
    .disc-item:last-child { border-bottom: none; }
    .disc-info { flex: 1; padding-right: 8px; }
    .disc-name { font-weight: 600; font-size: 0.92rem; color: #fff; }
    .disc-meta { font-size: 0.78rem; color: var(--muted); margin-top: 2px; }
    .disc-uid { font-family: monospace; color: var(--green); font-size: 0.78rem; }
    .disc-actions { display: flex; gap: 6px; }

    /* Toast */
    #toast { position: fixed; top: 16px; left: 50%; transform: translateX(-50%); background: var(--green); color: #000; font-weight: 700; padding: 10px 20px; border-radius: 30px; box-shadow: 0 4px 16px rgba(0,0,0,0.5); opacity: 0; pointer-events: none; transition: 0.3s; z-index: 999; }
    #toast.show { opacity: 1; transform: translateX(-50%) translateY(10px); }

    /* Exit Bar */
    .exit-bar { position: fixed; bottom: 0; left: 0; right: 0; background: #161616; border-top: 1px solid var(--border); padding: 12px 16px; text-align: center; }
  </style>
</head>
<body>
  <div id="toast"></div>

  <div class="header">
    <h1>📻 Vitrola Jukebox</h1>
    <p>Painel de Cadastro de Tags &amp; Modo Offline</p>
  </div>

  <!-- CARD 1: CADASTRAR DISCO -->
  <div class="card">
    <div class="card-title">
      <span>🏷️ Vincular Disco / Tag</span>
      <span style="font-size:0.75rem; color:var(--green); font-weight:normal;">100% Offline</span>
    </div>

    <!-- BADGE DE LEITURA EM TEMPO REAL -->
    <div id="tagBadge" class="tag-badge empty">
      <div class="tag-label">Leitor RFID no Prato:</div>
      <div id="tagVal" class="tag-val">Aproxime uma tag do prato...</div>
    </div>

    <form id="assignForm" onsubmit="saveDisc(event)">
      <label>Tag UID:</label>
      <input type="text" id="uidInput" placeholder="Ex: 043963AF8B2681" required>

      <label>Nome do Disco / Álbum:</label>
      <input type="text" id="nameInput" placeholder="Ex: Rock Classics" required>

      <label>Artista (opcional):</label>
      <input type="text" id="artistInput" placeholder="Ex: Queen">

      <label>Música no Cartão SD:</label>
      <input type="text" id="searchTrack" class="search-box" placeholder="🔍 Filtrar música ou artista..." oninput="filterTracks(this.value)">
      <select id="trackSelect" required onchange="onTrackSelected(this.value)">
        <option value="">Carregando faixas do cartão...</option>
      </select>

      <button type="submit" class="btn" id="btnSave">
        💾 Gravar no Cartão SD &amp; Testar
      </button>
    </form>
  </div>

  <!-- CARD 2: DISCOS CADASTRADOS NO CARTÃO SD -->
  <div class="card">
    <div class="card-title">
      <span>📂 Discos Cadastrados no SD</span>
      <button class="btn-secondary" onclick="loadDiscs()">🔄 Atualizar</button>
    </div>
    <div id="discsList" class="disc-list">
      <p style="text-align:center;color:var(--muted);padding:14px;font-size:0.85rem;">Carregando discos do cartão...</p>
    </div>
  </div>

  <!-- CARD 3: CONEXÃO WI-FI (OPCIONAL P/ BAIXAR MÚSICAS) -->
  <div class="card">
    <div class="card-title">
      <span>📶 Wi-Fi (Apenas p/ Baixar Músicas)</span>
    </div>
    <p style="font-size:0.8rem; color:var(--muted); margin-bottom:12px;">
      A vitrola funciona 100% offline. Conecte ao Wi-Fi apenas para sincronizar novas músicas do servidor.
    </p>
    <div id="wifiStatus" style="font-size:0.85rem; margin-bottom:10px; color:var(--amber);">Verificando Wi-Fi...</div>
    <div style="display:flex; gap:8px;">
      <input type="text" id="wifiSsid" placeholder="Nome da Rede Wi-Fi" style="margin-bottom:0; flex:1;">
      <input type="password" id="wifiPass" placeholder="Senha" style="margin-bottom:0; flex:1;">
    </div>
    <button class="btn-secondary" style="width:100%; margin-top:8px; padding:10px;" onclick="connectWifi()">
      Conectar Wi-Fi
    </button>
  </div>

  <!-- BARRA INFERIOR DE SAÍDA -->
  <div class="exit-bar">
    <button class="btn" style="background:#2d2d2d; color:#fff;" onclick="exitAp()">
      ✓ Concluir &amp; Voltar ao Modo Vitrola
    </button>
  </div>

  <script>
    let allTracks = [];
    let registeredDiscs = {};
    let lastSeenUid = "";

    function showToast(msg) {
      const t = document.getElementById('toast');
      t.textContent = msg;
      t.classList.add('show');
      setTimeout(() => t.classList.remove('show'), 2500);
    }

    // 1. Carrega catálogo de músicas
    async function loadTracks() {
      try {
        const res = await fetch('/api/tracks');
        allTracks = await res.json();
        renderTrackOptions(allTracks);
      } catch (e) {
        console.error('Erro ao carregar faixas', e);
      }
    }

    function renderTrackOptions(tracks) {
      const sel = document.getElementById('trackSelect');
      sel.innerHTML = '<option value="">-- Selecione a Música (' + tracks.length + ' disponíveis) --</option>';
      tracks.forEach(t => {
        const opt = document.createElement('option');
        opt.value = t.id;
        opt.textContent = t.n;
        sel.appendChild(opt);
      });
    }

    function filterTracks(query) {
      query = query.toLowerCase();
      const filtered = allTracks.filter(t => t.n.toLowerCase().includes(query) || t.id.toLowerCase().includes(query));
      renderTrackOptions(filtered);
    }

    function onTrackSelected(trackId) {
      if (!trackId) return;
      const t = allTracks.find(item => item.id === trackId);
      if (t) {
        const parts = t.n.split(' - ');
        if (!document.getElementById('nameInput').value) {
          document.getElementById('nameInput').value = parts[0].trim();
        }
        if (!document.getElementById('artistInput').value && parts.length > 1) {
          document.getElementById('artistInput').value = parts[1].trim();
        }
      }
    }

    // 2. Carrega lista de discos cadastrados em /discs.json
    async function loadDiscs() {
      try {
        const res = await fetch('/api/discs');
        registeredDiscs = await res.json();
        renderDiscsList();
      } catch (e) {
        console.error('Erro ao carregar discos', e);
      }
    }

    function renderDiscsList() {
      const c = document.getElementById('discsList');
      const keys = Object.keys(registeredDiscs);
      if (keys.length === 0) {
        c.innerHTML = '<p style="text-align:center;color:var(--muted);padding:14px;font-size:0.85rem;">Nenhum disco cadastrado no MicroSD ainda.</p>';
        return;
      }
      let html = '';
      keys.forEach(uid => {
        const d = registeredDiscs[uid];
        const name = d.name || 'Sem nome';
        const artist = d.artist || '';
        const track = d.track || (d.uri ? d.uri.replace('spotify:track:', '') : '');
        html += `
          <div class="disc-item">
            <div class="disc-info">
              <div class="disc-name">${name}</div>
              <div class="disc-meta">${artist ? artist + ' • ' : ''}<span class="disc-uid">${uid}</span></div>
            </div>
            <div class="disc-actions">
              <button class="btn-secondary" onclick="testPlay('${track}')" title="Testar Som">▶</button>
              <button class="btn-danger" onclick="deleteDisc('${uid}')" title="Excluir">🗑</button>
            </div>
          </div>
        `;
      });
      c.innerHTML = html;
    }

    // 3. Polling em tempo real da Tag no Prato
    async function pollStatus() {
      try {
        const res = await fetch('/api/status');
        const st = await res.json();

        const badge = document.getElementById('tagBadge');
        const tagVal = document.getElementById('tagVal');
        const uidInp = document.getElementById('uidInput');

        if (st.uid && st.uid !== 'NENHUM' && st.is_present) {
          badge.className = 'tag-badge detected';
          tagVal.innerHTML = '🟢 ' + st.uid + ' <span style="font-size:0.75rem;color:var(--muted);">(No Prato)</span>';

          if (st.uid !== lastSeenUid) {
            lastSeenUid = st.uid;
            uidInp.value = st.uid;

            // Se ja estiver cadastrado, preenche os campos
            if (registeredDiscs[st.uid]) {
              const d = registeredDiscs[st.uid];
              document.getElementById('nameInput').value = d.name || '';
              document.getElementById('artistInput').value = d.artist || '';
              const trk = d.track || (d.uri ? d.uri.replace('spotify:track:', '') : '');
              document.getElementById('trackSelect').value = trk;
            }
          }
        } else {
          badge.className = 'tag-badge empty';
          tagVal.textContent = 'Aproxime uma tag do prato...';
        }

        // Wi-Fi Status
        const ws = document.getElementById('wifiStatus');
        if (st.wifi_connected) {
          ws.style.color = 'var(--green)';
          ws.textContent = 'Conectado: ' + st.wifi_ssid + ' (IP: ' + st.wifi_ip + ')';
        } else {
          ws.style.color = 'var(--muted)';
          ws.textContent = 'Modo Atual: Offline [SD] (Wi-Fi Desconectado)';
        }
      } catch (e) {}
    }

    // 4. Salvar Disco no Cartão MicroSD
    async function saveDisc(ev) {
      ev.preventDefault();
      const uid = document.getElementById('uidInput').value.trim();
      const name = document.getElementById('nameInput').value.trim();
      const artist = document.getElementById('artistInput').value.trim();
      const track = document.getElementById('trackSelect').value;

      if (!uid || !track) {
        showToast('Preencha a Tag e escolha a música!');
        return;
      }

      const body = new URLSearchParams({ uid, name, artist, track });
      try {
        const res = await fetch('/api/save_disc', { method: 'POST', body });
        const data = await res.json();
        if (data.ok) {
          showToast('✓ Disco salvo no MicroSD com sucesso!');
          registeredDiscs[uid] = { name, artist, track, uri: 'spotify:track:' + track };
          renderDiscsList();
        } else {
          showToast('Erro ao salvar no cartão');
        }
      } catch (e) {
        showToast('Falha na comunicação');
      }
    }

    // 5. Excluir Disco
    async function deleteDisc(uid) {
      if (!confirm('Deseja remover o disco ' + uid + '?')) return;
      const body = new URLSearchParams({ uid });
      try {
        await fetch('/api/delete_disc', { method: 'POST', body });
        delete registeredDiscs[uid];
        renderDiscsList();
        showToast('Disco excluído do MicroSD.');
      } catch (e) {
        showToast('Erro ao excluir');
      }
    }

    // 6. Testar Reprodução
    async function testPlay(track) {
      if (!track) return;
      const body = new URLSearchParams({ track });
      fetch('/api/test_play', { method: 'POST', body });
      showToast('▶ Iniciando reprodução de teste...');
    }

    // 7. Conectar Wi-Fi
    async function connectWifi() {
      const ssid = document.getElementById('wifiSsid').value.trim();
      const pass = document.getElementById('wifiPass').value.trim();
      if (!ssid) return;
      const body = new URLSearchParams({ ssid, pass });
      fetch('/api/save_wifi', { method: 'POST', body });
      showToast('Tentando conectar ao Wi-Fi...');
    }

    // 8. Sair do Modo AP
    function exitAp() {
      fetch('/exit');
      showToast('Retornando ao Modo Vitrola...');
      setTimeout(() => { document.body.innerHTML = '<h2 style="text-align:center;margin-top:50px;color:var(--green);">✓ Modo Vitrola Ativado!<br><br><span style="font-size:1rem;color:#888;">Pode colocar o disco no prato!</span></h2>'; }, 600);
    }

    // Inicialização
    loadTracks();
    loadDiscs();
    setInterval(pollStatus, 1000);
  </script>
</body>
</html>
)rawliteral";
