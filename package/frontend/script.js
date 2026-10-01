const workspaceId = localStorage.getItem('filefind-workspace') || (crypto.randomUUID ? crypto.randomUUID() : `workspace-${Date.now()}-${Math.random().toString(16).slice(2)}`);
localStorage.setItem('filefind-workspace', workspaceId);
const apiBase = window.location.protocol === 'file:' ? 'http://localhost:8080' : '';
const state = { files: [], local: { selected: false, path: '', type: '' }, user: null };
let authToken = localStorage.getItem('filefind-auth-token') || '';
const $ = (selector) => document.querySelector(selector);
const $$ = (selector) => [...document.querySelectorAll(selector)];

document.addEventListener('mousemove', (e) => {
  const x = (e.clientX / window.innerWidth) * 100;
  const y = (e.clientY / window.innerHeight) * 100;
  document.body.style.setProperty('--mouse-x', `${x}%`);
  document.body.style.setProperty('--mouse-y', `${y}%`);
});

function authHeaders() {
  return authToken ? { 'Authorization': `Bearer ${authToken}` } : {};
}

function showAuth() {
  state.user = null;
  document.body.classList.add('locked');
  $$('.view').forEach(view => view.classList.toggle('active', view.id === 'auth'));
  $$('.nav').forEach(btn => btn.classList.remove('active'));
  const su = $('#sidebarUser');
  if (su) su.hidden = true;
  const topBtn = $('#topLoginBtn');
  if (topBtn) { topBtn.textContent = 'Login'; topBtn.style.display = 'block'; }
}

function showRegister() {
  state.user = null;
  document.body.classList.add('locked');
  $$('.view').forEach(view => view.classList.toggle('active', view.id === 'register'));
  $$('.nav').forEach(btn => btn.classList.remove('active'));
  const su = $('#sidebarUser');
  if (su) su.hidden = true;
}

function showApp(user) {
  state.user = user || null;
  document.body.classList.remove('locked');
  const su = $('#sidebarUser');
  if (su) su.hidden = !state.user;
  const label = $('#userEmailLabel');
  if (label) label.textContent = state.user ? state.user.email : '';
  const note = document.querySelector('.sidebar-note span');
  if (note) note.textContent = state.user ? `Private files for ${state.user.email}` : 'Files stay on this machine.';
  const topBtn = $('#topLoginBtn');
  if (topBtn) { topBtn.textContent = state.user ? state.user.email : 'Login'; }
  showView('dashboard');
  refreshFiles().catch(err => toast(err.message));
  refreshStats().catch(err => toast(err.message));
  refreshTrash().catch(err => toast(err.message));
}

function toast(message) {
  const node = $('#toast');
  node.textContent = message;
  node.classList.add('show');
  setTimeout(() => node.classList.remove('show'), 2600);
}

function showView(id) {
  if (!state.user && id !== 'auth' && id !== 'register') { showAuth(); return; }
  $$('.view').forEach(view => view.classList.toggle('active', view.id === id));
  $$('.nav').forEach(btn => btn.classList.toggle('active', btn.dataset.view === id));
}

function fmtBytes(bytes) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
  if (bytes < 1024 * 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MB`;
  return `${(bytes / 1024 / 1024 / 1024).toFixed(2)} GB`;
}

function workspaceUrl(path) {
  const sep = path.includes('?') ? '&' : '?';
  let url = `${apiBase}${path}${sep}workspace=${encodeURIComponent(workspaceId)}`;
  if (authToken) url += `&token=${encodeURIComponent(authToken)}`;
  return url;
}

async function api(path, options = {}) {
  let res;
  try {
    res = await fetch(`${apiBase}${path}`, {
      cache: 'no-store',
      ...options,
      headers: { 'X-Workspace-Id': workspaceId, ...authHeaders(), ...(options.headers || {}) }
    });
  } catch (error) {
    $('#connectionStatus').className = 'connection offline';
    $('#connectionStatus').textContent = 'Server unavailable. Open FileFind at http://localhost:8080.';
    throw new Error('Cannot reach the FileFind server. Start FileFind and open http://localhost:8080.');
  }
  const text = await res.text();
  let data;
  try { data = text ? JSON.parse(text) : {}; } catch (error) { throw new Error('The server returned an invalid response.'); }
  if (res.status === 401) {
    authToken = '';
    localStorage.removeItem('filefind-auth-token');
    showAuth();
    throw new Error(data.error || 'Please login to continue.');
  }
  if (!res.ok) throw new Error(data.error || 'Server error. Please try again.');
  $('#connectionStatus').className = 'connection online';
  $('#connectionStatus').textContent = 'Local index connected';
  return data;
}

function uploadRequest(form) {
  return new Promise((resolve, reject) => {
    const request = new XMLHttpRequest();
    request.open('POST', `${apiBase}/api/upload`);
    request.setRequestHeader('X-Workspace-Id', workspaceId);
    if (authToken) request.setRequestHeader('Authorization', `Bearer ${authToken}`);
    request.upload.addEventListener('progress', event => {
      if (event.lengthComputable) $('#uploadProgress span').style.width = `${Math.max(8, Math.round(event.loaded / event.total * 78))}%`;
    });
    request.addEventListener('load', () => {
      let data;
      try { data = request.responseText ? JSON.parse(request.responseText) : {}; } catch (error) { reject(new Error('The server returned an invalid response.')); return; }
      if (request.status < 200 || request.status >= 300) { reject(new Error(data.error || 'Server error. Please try again.')); return; }
      $('#connectionStatus').className = 'connection online';
      $('#connectionStatus').textContent = 'Local index connected';
      resolve(data);
    });
    request.addEventListener('error', () => reject(new Error('Cannot reach the FileFind server. Start FileFind and open http://localhost:8080.')));
    request.addEventListener('abort', () => reject(new Error('Upload cancelled.')));
    request.send(form);
  });
}

async function refreshFiles() {
  const data = await api('/api/files');
  state.files = data.files || [];
  renderFiles();
  return state.files;
}

async function refreshStats() {
  const stats = await api('/api/stats');
  $('#statFiles').textContent = stats.totalFiles;
  $('#statPdf').textContent = stats.pdfFiles;
  $('#statDocx').textContent = stats.docxFiles;
  $('#statPptx').textContent = stats.pptxFiles;
  $('#statSearches').textContent = stats.totalSearches;
  $('#statMatches').textContent = stats.totalMatches;
  $('#statWords').textContent = `${stats.totalWords.toLocaleString()} extracted words`;
  const recent = $('#recentSearches');
  recent.innerHTML = '';
  (stats.recentSearches || []).forEach(item => {
    const chip = document.createElement('span');
    chip.textContent = item;
    recent.appendChild(chip);
  });
  if (!recent.children.length) recent.textContent = 'No searches yet.';
}

function renderFiles() {
  const body = $('#fileRows');
  const label = $('#fileCountLabel');
  if (label) label.textContent = `${state.files.length} file${state.files.length === 1 ? '' : 's'} ready to search`;
  body.innerHTML = '';
  if (!state.files.length) {
    body.innerHTML = '<tr><td colspan="9">No uploaded files yet.</td></tr>';
    return;
  }
  state.files.forEach(file => {
    const tr = document.createElement('tr');
    tr.innerHTML = `
      <td><strong>${escapeHtml(file.originalName)}</strong></td>
      <td>${escapeHtml(file.folder || 'Inbox')}</td>
      <td>${escapeHtml(file.tags || '-')}</td>
      <td><span class="type-badge">${file.type.toUpperCase()}</span></td>
      <td>${fmtBytes(file.size)}</td>
      <td><span class="status ${file.status === 'Processed' ? 'ok' : 'warn'}">${escapeHtml(file.status)}</span></td>
      <td>${Number(file.wordCount).toLocaleString()}</td>
      <td>${escapeHtml(file.uploadedAt)}</td>
      <td class="file-actions">
        <button class="small" data-action="open" data-id="${file.id}">Preview</button>
        <button class="small" data-action="download" data-id="${file.id}">Download</button>
        <button class="small" data-action="rename" data-id="${file.id}">Rename</button>
        <button class="danger small" data-action="delete" data-id="${file.id}">Delete</button>
      </td>
    `;
    body.appendChild(tr);
  });
}

async function renameFile(id) {
  const file = state.files.find(item => item.id === id);
  const name = window.prompt('New file name (keep .' + file.type + '):', file.originalName);
  if (!name) return;
  const folder = window.prompt('Folder:', file.folder || 'Inbox') || 'Inbox';
  const tags = window.prompt('Tags (comma separated):', file.tags || '') ?? (file.tags || '');
  if (name === file.originalName && folder === file.folder && tags === file.tags) return;
  await api(`/api/files/${id}`, {
    method: 'PATCH',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ name, folder, tags })
  });
  await refreshFiles();
  toast('File renamed.');
}

function renderUploadedPreview(uploaded, errors) {
  const preview = $('#uploadedPreview');
  preview.innerHTML = '';
  if (!uploaded.length && !errors.length) return;
  const title = document.createElement('h3');
  title.textContent = uploaded.length ? 'Uploaded successfully' : 'Upload needs attention';
  preview.appendChild(title);
  uploaded.forEach(file => {
    const row = document.createElement('div');
    row.className = 'upload-card';
    row.innerHTML = `<strong>${escapeHtml(file.originalName)}</strong><span>${file.type.toUpperCase()} | ${fmtBytes(file.size)} | ${Number(file.wordCount).toLocaleString()} words | ${escapeHtml(file.status)}</span>`;
    preview.appendChild(row);
  });
  errors.forEach(error => {
    const row = document.createElement('div');
    row.className = 'upload-card error';
    row.textContent = error;
    preview.appendChild(row);
  });
}

function escapeHtml(value) {
  return String(value).replace(/[&<>"']/g, c => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
  }[c]));
}

async function uploadFiles(files) {
  const allowed = ['pdf', 'docx', 'pptx', 'txt', 'csv', 'md', 'json', 'log', 'rtf', 'jpg', 'jpeg', 'png', 'gif', 'bmp', 'webp'];
  const selected = [...files];
  const accepted = selected.filter(file => allowed.includes(file.name.split('.').pop().toLowerCase()));
  const rejected = selected.filter(file => !allowed.includes(file.name.split('.').pop().toLowerCase()));
  if (!accepted.length) {
    renderUploadedPreview([], rejected.map(file => `${file.name}: unsupported file type`));
    toast('Unsupported file type.');
    return;
  }
  const uploaded = [];
  const errors = [...rejected.map(file => `${file.name}: unsupported file type`)];
  const MAX_UPLOAD = 5 * 1024 * 1024 * 1024; // 5 GB
  const tooBig = accepted.filter(file => file.size > MAX_UPLOAD);
  const fitting = accepted.filter(file => file.size <= MAX_UPLOAD);
  errors.push(...tooBig.map(file => `${file.name}: file exceeds 5 GB`));
  if (!fitting.length) {
    renderUploadedPreview([], errors);
    toast('File exceeds 5 GB limit.');
    return;
  }
  $('#uploadMessages').textContent = `Uploading 0 of ${fitting.length} files...`;
  try {
    for (let index = 0; index < fitting.length; index += 1) {
      const form = new FormData();
      form.append('files', fitting[index], fitting[index].name);
      $('#uploadMessages').textContent = `Uploading ${index + 1} of ${fitting.length}: ${fitting[index].name}`;
      const result = await uploadRequest(form);
      uploaded.push(...(result.uploaded || []));
      errors.push(...(result.errors || []));
    }
    $('#uploadProgress span').style.width = '100%';
    $('#uploadMessages').textContent = uploaded.length
      ? `${uploaded.length} file${uploaded.length === 1 ? '' : 's'} uploaded. Opening My Files...`
      : 'No files were uploaded.';
    renderUploadedPreview(uploaded, errors);
    await refreshFiles();
    await refreshStats();
    toast(`${uploaded.length} file${uploaded.length === 1 ? '' : 's'} uploaded`);
    if (uploaded.length) showView('files');
  } catch (err) {
    $('#uploadMessages').textContent = err.message;
    renderUploadedPreview([], [err.message]);
    toast(err.message);
  } finally {
    $('#fileInput').value = '';
    setTimeout(() => $('#uploadProgress span').style.width = '0', 900);
  }
}

function searchPayload(queryOverride) {
  const mode = $('#mode').value;
  const algorithm = mode === 'regex' ? 'regex' : $('#algorithm').value;
  return {
    query: queryOverride || $('#query').value.trim(),
    algorithm,
    mode,
    caseSensitive: $('#caseSensitive').checked,
    fileTypes: $$('.typeFilter:checked').map(input => input.value),
    fileIds: []
  };
}

function selectedSearchSource() {
  return document.querySelector('input[name="searchSource"]:checked')?.value || 'uploaded';
}

function updateLocalLocation(label, selected) {
  $('#localLocation').textContent = label;
  $('#scanLocal').disabled = !selected;
}

async function selectLocalFolder() {
  const result = await api('/api/explorer/select-folder', { method: 'POST' });
  if (result.cancelled) return;
  state.local = { selected: true, path: result.path, type: 'folder' };
  updateLocalLocation(`Selected folder: ${result.path}`, true);
  $('#localScanStatus').textContent = 'Choose Scan selected location to index supported files.';
  document.querySelector('input[name="searchSource"][value="local"]').checked = true;
}

async function selectLocalFiles() {
  const result = await api('/api/explorer/select-files', { method: 'POST' });
  if (result.cancelled) return;
  state.local = { selected: true, path: `${result.files.length} selected file${result.files.length === 1 ? '' : 's'}`, type: 'files' };
  updateLocalLocation(state.local.path, true);
  $('#localScanStatus').textContent = `${result.filesFound} supported file${result.filesFound === 1 ? '' : 's'} ready to search.`;
  document.querySelector('input[name="searchSource"][value="local"]').checked = true;
}

async function scanLocal() {
  if (!state.local.selected || state.local.type !== 'folder') return;
  $('#localScanStatus').textContent = 'Scanning selected folder...';
  const result = await api('/api/explorer/scan', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ includeSubfolders: $('#includeSubfolders').checked })
  });
  $('#localScanStatus').textContent = `${result.filesFound} supported file${result.filesFound === 1 ? '' : 's'} ready to search. ${result.warnings?.length || 0} skipped.`;
}

async function runSearch(queryOverride) {
  const payload = searchPayload(queryOverride);
  if (!payload.query) {
    toast('Search query cannot be empty.');
    return;
  }
  const endpoint = selectedSearchSource() === 'local' ? '/api/search/local' : '/api/search';
  const data = await api(endpoint, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
  renderResults(data, endpoint.endsWith('/local'));
  await refreshStats();
  showView('search');
}

function renderResults(data, isLocal = false) {
  const summary = $('#searchSummary');
  summary.style.display = 'block';
  summary.innerHTML = `
    Query: <strong>${escapeHtml(data.query)}</strong> &nbsp;
    Algorithm: <strong>${escapeHtml(data.algorithm)}</strong> &nbsp;
    Files searched: <strong>${data.filesSearched}</strong> &nbsp;
    Matches: <strong>${data.totalMatches}</strong> &nbsp;
    Characters scanned: <strong>${Number(data.charactersScanned).toLocaleString()}</strong> &nbsp;
    Time: <strong>${Number(data.executionTime).toFixed(3)} ms</strong>
  `;
  const results = $('#results');
  results.innerHTML = '';
  if (!data.results.length) {
    results.innerHTML = '<div class="result">No matching results found.</div>';
    return;
  }
  data.results.forEach(file => {
    const node = document.createElement('article');
    node.className = 'result';
    node.innerHTML = `
      <div class="result-head">
        <h3>${escapeHtml(file.fileName)}</h3>
        ${isLocal
          ? `<div class="result-actions"><button class="small" data-local-open="${escapeHtml(file.filePath)}" type="button">Open file</button><button class="small secondary" data-local-show="${escapeHtml(file.filePath)}" type="button">Show in Explorer</button></div>`
          : `<button class="small result-open" data-open-result="${escapeHtml(file.fileId)}" type="button">Open file</button>`}
      </div>
      <p>${file.type.toUpperCase()} | ${isLocal ? escapeHtml(file.filePath) : (file.nameMatch ? 'Filename or content match' : 'Content match')} | Matches: ${file.matches}</p>
      ${(file.contexts || []).map(ctx => `<div class="context">${ctx}</div>`).join('')}
    `;
    results.appendChild(node);
  });
}

async function runComparison() {
  const query = $('#compareQuery').value.trim();
  if (!query) {
    toast('Search query cannot be empty.');
    return;
  }
  const payload = searchPayload(query);
  payload.mode = 'simple';
  const data = await api('/api/compare', {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(payload)
  });
  const rows = $('#comparisonRows');
  rows.innerHTML = '';
  const maxTime = Math.max(1, ...data.comparisons.map(row => row.executionTime));
  $('#chart').innerHTML = '';
  data.comparisons.forEach(row => {
    rows.insertAdjacentHTML('beforeend', `
      <tr><td>${escapeHtml(row.algorithm)}</td><td>${row.matches}</td><td>${Number(row.executionTime).toFixed(3)} ms</td></tr>
    `);
    $('#chart').insertAdjacentHTML('beforeend', `
      <div class="bar"><strong>${escapeHtml(row.algorithm)}</strong><span style="width:${Math.max(4, row.executionTime / maxTime * 100)}%"></span><em>${Number(row.executionTime).toFixed(3)} ms</em></div>
    `);
  });
}

async function refreshTrash() {
  const data = await api('/api/trash');
  const node = $('#trashRows');
  node.innerHTML = '';
  if (!data.files?.length) { node.textContent = 'Recycle bin is empty.'; return; }
  data.files.forEach(file => {
    const row = document.createElement('div');
    row.className = 'upload-card';
    row.innerHTML = `<strong>${escapeHtml(file.originalName)}</strong><span>${escapeHtml(file.uploadedAt)} <button class="small" data-restore="${file.id}">Restore</button></span>`;
    node.appendChild(row);
  });
}

function exportFiles() {
  const rows = [['name', 'type', 'size', 'folder', 'tags', 'status', 'uploaded']];
  state.files.forEach(file => rows.push([file.originalName, file.type, file.size, file.folder || 'Inbox', file.tags || '', file.status, file.uploadedAt]));
  const csv = rows.map(row => row.map(value => `"${String(value).replaceAll('"', '""')}"`).join(',')).join('\n');
  const link = document.createElement('a');
  link.href = URL.createObjectURL(new Blob([csv], { type: 'text/csv' }));
  link.download = 'filefind-files.csv';
  link.click();
  URL.revokeObjectURL(link.href);
}

function applyTheme(light) {
  document.body.classList.toggle('light-theme', light);
  localStorage.setItem('filefind-theme', light ? 'light' : 'dark');
}

async function authRequest(path, email, password, errorNode) {
  errorNode.textContent = '';
  const res = await fetch(`${apiBase}${path}`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ email, password })
  });
  const text = await res.text();
  let data = {};
  try { data = text ? JSON.parse(text) : {}; } catch (e) { throw new Error('Invalid server response.'); }
  if (!res.ok) throw new Error(data.error || 'Authentication failed.');
  authToken = data.token;
  localStorage.setItem('filefind-auth-token', authToken);
  showApp(data.user);
  toast(`Welcome, ${data.user.email}`);
}

async function checkSession() {
  if (!authToken) { showAuth(); return; }
  try {
    const res = await fetch(`${apiBase}/api/auth/me`, { headers: { ...authHeaders() } });
    if (!res.ok) throw new Error('expired');
    const data = await res.json();
    showApp(data.user);
  } catch (e) {
    authToken = '';
    localStorage.removeItem('filefind-auth-token');
    showAuth();
  }
}

async function doLogout() {
  try {
    await fetch(`${apiBase}/api/auth/logout`, { method: 'POST', headers: { ...authHeaders() } });
  } catch (e) {}
  authToken = '';
  localStorage.removeItem('filefind-auth-token');
  state.files = [];
  showAuth();
  toast('Logged out.');
}

function bindEvents() {
  $$('.nav').forEach(btn => btn.addEventListener('click', () => showView(btn.dataset.view)));
  $('#welcomeUpload').addEventListener('click', () => showView('upload'));
  $('#welcomeSearch').addEventListener('click', () => showView('search'));
  $('#browseBtn').addEventListener('click', () => $('#fileInput').click());
  $('#folderBtn').addEventListener('click', () => $('#folderInput').click());
  $('#fileInput').addEventListener('change', event => uploadFiles(event.target.files));
  $('#folderInput').addEventListener('change', event => uploadFiles(event.target.files));
  ['dragenter', 'dragover'].forEach(type => $('#dropZone').addEventListener(type, event => {
    event.preventDefault();
    $('#dropZone').classList.add('drag');
  }));
  ['dragleave', 'drop'].forEach(type => $('#dropZone').addEventListener(type, event => {
    event.preventDefault();
    $('#dropZone').classList.remove('drag');
  }));
  $('#dropZone').addEventListener('drop', event => uploadFiles(event.dataTransfer.files));
  $('#fileRows').addEventListener('click', async event => {
    const id = event.target.dataset.id;
    const action = event.target.dataset.action;
    if (!id) return;
    try {
      if (action === 'open') window.open(workspaceUrl(`/api/files/${id}/open`), '_blank', 'noopener');
      if (action === 'download') window.location.href = workspaceUrl(`/api/files/${id}/download`);
      if (action === 'rename') await renameFile(id);
      if (action === 'delete') {
        await api(`/api/files/${id}`, { method: 'DELETE' });
        toast('File deleted.');
        await refreshFiles();
        await refreshStats();
      }
    } catch (error) { toast(error.message); }
  });
  $('#trashRows').addEventListener('click', async event => {
    const id = event.target.dataset.restore;
    if (!id) return;
    await api(`/api/files/${id}/restore`, { method: 'POST' });
    await refreshTrash();
    await refreshFiles();
    await refreshStats();
    toast('File restored.');
  });
  $('#results').addEventListener('click', event => {
    const id = event.target.dataset.openResult;
    if (id) window.open(workspaceUrl(`/api/files/${id}/open`), '_blank', 'noopener');
    const path = event.target.dataset.localOpen || event.target.dataset.localShow;
    if (!path) return;
    const endpoint = event.target.dataset.localOpen ? '/api/explorer/open-file' : '/api/explorer/show-in-explorer';
    api(endpoint, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify({ path })
    }).catch(error => toast(error.message));
  });
  $('#searchForm').addEventListener('submit', event => {
    event.preventDefault();
    runSearch().catch(err => toast(err.message));
  });
  $('#quickSearch').addEventListener('submit', event => {
    event.preventDefault();
    $('#query').value = $('#quickQuery').value;
    runSearch($('#quickQuery').value.trim()).catch(err => toast(err.message));
  });
  $('#compareForm').addEventListener('submit', event => {
    event.preventDefault();
    runComparison().catch(err => toast(err.message));
  });
  $('#defaultCase').addEventListener('change', event => {
    $('#caseSensitive').checked = event.target.checked;
  });
  $('#exportFiles').addEventListener('click', exportFiles);
  $('#themeToggle').addEventListener('click', () => applyTheme(!document.body.classList.contains('light-theme')));
  $('#refreshTrash').addEventListener('click', () => refreshTrash().catch(err => toast(err.message)));
  $('#loginForm').addEventListener('submit', async event => {
    event.preventDefault();
    try {
      await authRequest('/api/auth/login', $('#loginEmail').value.trim(), $('#loginPassword').value, $('#loginError'));
    } catch (err) { $('#loginError').textContent = err.message; }
  });
  $('#signupForm').addEventListener('submit', async event => {
    event.preventDefault();
    try {
      const pw = $('#signupPassword').value;
      const confirm = $('#signupConfirm').value;
      if (pw !== confirm) { $('#signupError').textContent = 'Passwords do not match.'; return; }
      await authRequest('/api/auth/signup', $('#signupEmail').value.trim(), pw, $('#signupError'));
    } catch (err) { $('#signupError').textContent = err.message; }
  });
  const gotoReg = $('#gotoRegisterBtn');
  if (gotoReg) gotoReg.addEventListener('click', showRegister);
  const backLogin = $('#backToLoginBtn');
  if (backLogin) backLogin.addEventListener('click', showAuth);
  $('#logoutBtn').addEventListener('click', doLogout);
  const topLogin = $('#topLoginBtn');
  if (topLogin) topLogin.addEventListener('click', () => { if (state.user) showView('settings'); else showAuth(); });
}

const lightTheme = localStorage.getItem('filefind-theme') === 'light';
applyTheme(lightTheme);
bindEvents();
checkSession();