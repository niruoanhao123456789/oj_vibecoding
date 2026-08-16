// admin.js — 管理端逻辑（阶段 8：教师班级管理 + 管理员用户管理/配置 + 题目管理）
'use strict';

// ── 确认弹窗 ────────────────────────────────────────────────
// 需要二次确认时调用。confirm 后执行 onConfirm()。
function openConfirm({ message, warning, okText, onConfirm }) {
  document.getElementById('confirm-message').textContent = message || '';
  const warn = document.getElementById('confirm-warning');
  if (warning) {
    warn.textContent = warning;
    warn.style.display = 'block';
  } else {
    warn.style.display = 'none';
  }
  document.getElementById('confirm-ok').textContent = okText || '确认执行';
  const modal = document.getElementById('confirm-modal');
  modal.style.display = 'flex';

  document.getElementById('confirm-ok').onclick = () => {
    modal.style.display = 'none';
    onConfirm && onConfirm();
  };
  document.getElementById('confirm-cancel').onclick = () => {
    modal.style.display = 'none';
  };
}

// ── 教师：班级管理 ──────────────────────────────────────────
function renderClass(cls) {
  const el = document.getElementById('class-info');
  if (!cls || !cls.id) {
    el.innerHTML = '<p class="muted">尚未创建班级。</p>';
    return;
  }
  const members = (cls.members || [])
    .map((m) => '<li>' + escapeHtml(m.username) + '</li>')
    .join('');
  el.innerHTML =
    '<p>班级：<strong>' + escapeHtml(cls.name) + '</strong></p>' +
    '<p>邀请码：<code class="invite-code">' + escapeHtml(cls.invite_code) +
    '</code>（分享给学生，学生凭此加入）</p>' +
    '<p>成员（' + (cls.members || []).length + ' 人）：</p>' +
    '<ul class="member-list">' + (members || '<li class="muted">暂无成员</li>') + '</ul>';
}

async function loadClass() {
  try {
    const data = await api('/api/admin/class');
    renderClass(data.data && data.data.class);
  } catch (err) {
    const el = document.getElementById('alert');
    showAlert(el, 'error', err.message);
  }
}

async function createClass(e) {
  e.preventDefault();
  const el = document.getElementById('alert');
  hideAlert(el);
  const name = document.getElementById('class-name').value.trim();
  const btn = document.getElementById('class-submit');
  btn.disabled = true;
  try {
    const data = await api('/api/admin/class', {
      method: 'POST',
      body: name ? { name } : {},
    });
    renderClass(data.data && data.data.class);
    showAlert(el, 'success', '班级就绪');
  } catch (err) {
    showAlert(el, 'error', err.message);
  } finally {
    btn.disabled = false;
  }
}

async function resetInvite() {
  const el = document.getElementById('alert');
  hideAlert(el);
  try {
    const data = await api('/api/admin/class/invite', { method: 'POST' });
    renderClass(data.data && data.data.class);
    showAlert(el, 'success', '邀请码已重置');
  } catch (err) {
    showAlert(el, 'error', err.message);
  }
}

// ── 管理员：用户管理 ────────────────────────────────────────
const ROLE_TEXT = { student: '学生', teacher: '教师', admin: '管理员' };

function roleOptions(cur) {
  return ['student', 'teacher', 'admin']
    .map((r) =>
      '<option value="' + r + '"' + (r === cur ? ' selected' : '') + '>' +
      (ROLE_TEXT[r] || r) + '</option>')
    .join('');
}

async function loadUsers() {
  const tbody = document.getElementById('user-body');
  try {
    const data = await api('/api/admin/users');
    const users = (data.data && data.data.users) || [];
    tbody.innerHTML = '';
    users.forEach((u) => {
      const tr = document.createElement('tr');
      const hasClass = u.has_class ? '（' + escapeHtml(u.class_name || '?') + '）' : '';
      tr.innerHTML =
        '<td>' + u.id + '</td>' +
        '<td>' + escapeHtml(u.username) + '</td>' +
        '<td><select data-role="' + u.id + '">' + roleOptions(u.role) + '</select></td>' +
        '<td>' + (u.status === 1
          ? '<span class="badge badge-ac">正常</span>'
          : '<span class="badge badge-wa">已禁用</span>') + '</td>' +
        '<td class="muted">' + (u.has_class ? '有' + hasClass : '—') + '</td>' +
        '<td>' +
        '<button class="btn btn-sm" data-save="' + u.id + '" style="margin-right:6px;">保存角色</button>' +
        (u.status === 1
          ? '<button class="btn btn-sm" data-disable="' + u.id + '" style="background:var(--danger);margin-right:6px;">禁用</button>'
          : '<button class="btn btn-sm" data-enable="' + u.id + '" style="background:var(--success);margin-right:6px;">启用</button>') +
        '<button class="btn btn-sm" data-del="' + u.id + '" style="background:var(--danger);">删除</button>' +
        '</td>';
      tbody.appendChild(tr);
    });
  } catch (err) {
    const el = document.getElementById('alert');
    showAlert(el, 'error', err.message);
  }
}

async function createUser(e) {
  e.preventDefault();
  const el = document.getElementById('alert');
  hideAlert(el);
  const username = document.getElementById('user-username').value.trim();
  const password = document.getElementById('user-password').value;
  const role = document.getElementById('user-role').value;
  if (!username || !password) {
    showAlert(el, 'error', '请填写用户名与密码');
    return;
  }
  try {
    await api('/api/admin/users', {
      method: 'POST',
      body: { username, password, role },
    });
    showAlert(el, 'success', '用户已创建');
    document.getElementById('user-username').value = '';
    document.getElementById('user-password').value = '';
    loadUsers();
  } catch (err) {
    showAlert(el, 'error', err.message);
  }
}

// 用户操作：保存角色 / 禁用 / 启用 / 删除
function handleUserAction(btn) {
  const id = btn.dataset.save || btn.dataset.disable || btn.dataset.enable ||
    btn.dataset.del;
  const el = document.getElementById('alert');
  hideAlert(el);
  const target = document.querySelector('[data-role="' + id + '"]');

  const doRole = () => {
    const role = target ? target.value : '';
    api('/api/admin/users/' + id, {
      method: 'PUT',
      body: { role },
    }).then(() => {
      showAlert(el, 'success', '角色已更新');
      loadUsers();
    }).catch((err) => showAlert(el, 'error', err.message));
  };

  const doDisable = () =>
    api('/api/admin/users/' + id, { method: 'PUT', body: { status: 0 } })
      .then(() => { showAlert(el, 'success', '已禁用'); loadUsers(); })
      .catch((err) => showAlert(el, 'error', err.message));

  const doEnable = () =>
    api('/api/admin/users/' + id, { method: 'PUT', body: { status: 1 } })
      .then(() => { showAlert(el, 'success', '已启用'); loadUsers(); })
      .catch((err) => showAlert(el, 'error', err.message));

  const doDelete = () =>
    api('/api/admin/users/' + id, { method: 'DELETE' })
      .then(() => { showAlert(el, 'success', '用户已删除'); loadUsers(); })
      .catch((err) => showAlert(el, 'error', err.message));

  // 降级为 student 或删除：若目标有班级需二次确认（将一并删除班级）
  const isDemoteOrDelete = (btn.dataset.save && target &&
    target.value === 'student') || btn.dataset.del;

  if (isDemoteOrDelete) {
    const row = btn.closest('tr');
    const hasClass = row && row.cells[4] && row.cells[4].textContent.includes('有');
    if (hasClass) {
      openConfirm({
        message: '该用户是教师且拥有班级。降级或删除将同时删除其班级及所有成员，且不可恢复。',
        warning: '此操作会删除班级及班级成员数据，请确认。',
        okText: btn.dataset.del ? '确认删除用户及班级' : '确认降级并删除班级',
        onConfirm: btn.dataset.del ? doDelete : doRole,
      });
      return;
    }
  }

  if (btn.dataset.del) {
    openConfirm({
      message: '确定删除该用户吗？其提交记录将一并删除。',
      okText: '确认删除',
      onConfirm: doDelete,
    });
  } else if (btn.dataset.disable) {
    doDisable();
  } else if (btn.dataset.enable) {
    doEnable();
  } else {
    doRole();
  }
}

// ── 管理员：系统配置 ────────────────────────────────────────
async function loadConfig() {
  try {
    const data = await api('/api/admin/config');
    document.getElementById('config-code').value =
      (data.data && data.data.teacher_invite_code) || '';
  } catch (err) {
    const el = document.getElementById('alert');
    showAlert(el, 'error', err.message);
  }
}

async function saveConfig(e) {
  e.preventDefault();
  const el = document.getElementById('alert');
  hideAlert(el);
  const code = document.getElementById('config-code').value.trim();
  if (!code) {
    showAlert(el, 'error', '邀请码不能为空');
    return;
  }
  try {
    await api('/api/admin/config', {
      method: 'PUT',
      body: { teacher_invite_code: code },
    });
    showAlert(el, 'success', '教师邀请码已更新');
  } catch (err) {
    showAlert(el, 'error', err.message);
  }
}

// ── 教师/管理员：题目管理（表单化编辑） ─────────────────────
let allProblems = [];
let editingProblemId = null;
let caseSeq = 0;

// 生成一个测试用例行的 HTML
function caseRowHTML(num, input, output, score) {
  const scoreVal = (score === '' || score === null || score === undefined) ? '' : String(score);
  return '<div class="case-row" data-case-row="' + num + '">' +
    '<div class="case-head">' +
    '<span class="case-title">测试点 #' + num + '</span>' +
    '<button type="button" class="btn btn-sm" data-case-del="' + num +
    '" style="background:var(--danger);">删除</button>' +
    '</div>' +
    '<div class="case-fields">' +
    '<div class="form-group"><label>输入</label>' +
    '<textarea data-case-input="' + num + '" rows="3">' + escapeHtml(input) + '</textarea></div>' +
    '<div class="form-group"><label>期望输出</label>' +
    '<textarea data-case-output="' + num + '" rows="3">' + escapeHtml(output) + '</textarea></div>' +
    '<div class="form-group" style="flex:0 0 140px;min-width:140px;"><label>分值（可选）</label>' +
    '<input type="number" data-case-score="' + num + '" value="' + escapeHtml(scoreVal) + '"></div>' +
    '</div></div>';
}

function addCaseRow(input, output, score) {
  caseSeq += 1;
  const box = document.getElementById('case-editor');
  const wrap = document.createElement('div');
  wrap.innerHTML = caseRowHTML(caseSeq, input || '', output || '', score);
  box.appendChild(wrap.firstChild);
}

// 收集表单字段 → 题目 JSON 对象
function collectProblemJSON() {
  return {
    title: document.getElementById('prob-title').value.trim(),
    description: document.getElementById('prob-desc').value,
    sample_in: document.getElementById('prob-sample-in').value,
    sample_out: document.getElementById('prob-sample-out').value,
    time_limit_ms: parseInt(document.getElementById('prob-time').value, 10),
    memory_limit_mb: parseInt(document.getElementById('prob-mem').value, 10),
    difficulty: parseInt(document.getElementById('prob-difficulty').value, 10),
  };
}

function collectCases() {
  const cases = [];
  const rows = document.querySelectorAll('#case-editor [data-case-row]');
  rows.forEach((row) => {
    const input = row.querySelector('[data-case-input]').value;
    const output = row.querySelector('[data-case-output]').value;
    const scoreEl = row.querySelector('[data-case-score]');
    const score = (scoreEl && scoreEl.value !== '')
      ? parseInt(scoreEl.value, 10) : null;
    cases.push({ input, output, score });
  });
  return cases;
}

function validateProblemForm(json, cases) {
  if (!json.title) throw new Error('标题不能为空');
  if (json.title.length > 255) throw new Error('标题过长（≤255 字符）');
  if (!isFinite(json.time_limit_ms) || json.time_limit_ms <= 0) {
    throw new Error('CPU 时限必须为正整数');
  }
  if (!isFinite(json.memory_limit_mb) || json.memory_limit_mb <= 0) {
    throw new Error('内存上限必须为正整数');
  }
  if (json.difficulty < 1 || json.difficulty > 3) {
    throw new Error('难度取值必须为 1-3');
  }
  if (!editingProblemId && cases.length === 0) {
    throw new Error('新建题目至少需要 1 个测试点');
  }
  cases.forEach((c, i) => {
    if (c.input === '' && c.output === '') {
      throw new Error('测试点 #' + (i + 1) + '：输入与期望输出不能同时为空');
    }
    if (c.score !== null && isNaN(c.score)) {
      throw new Error('测试点 #' + (i + 1) + '：分值必须为整数');
    }
  });
}

function resetProblemForm() {
  editingProblemId = null;
  caseSeq = 0;
  document.getElementById('problem-form-title').textContent = '发布新题目';
  document.getElementById('problem-edit-id').textContent = '';
  document.getElementById('prob-title').value = '';
  document.getElementById('prob-difficulty').value = '1';
  document.getElementById('prob-time').value = '1000';
  document.getElementById('prob-mem').value = '256';
  document.getElementById('prob-sample-in').value = '';
  document.getElementById('prob-sample-out').value = '';
  document.getElementById('prob-desc').value = '';
  document.getElementById('case-editor').innerHTML = '';
  addCaseRow();
  document.getElementById('problem-import-btn').style.display = 'inline-block';
  document.getElementById('problem-update-btn').style.display = 'none';
  document.getElementById('problem-cancel-btn').style.display = 'none';
}

async function loadProblems() {
  const el = document.getElementById('problem-list');
  try {
    const data = await api('/api/problems');
    allProblems = (data.data && data.data.problems) || [];
    if (allProblems.length === 0) {
      el.innerHTML = '<p class="muted">暂无题目。</p>';
      return;
    }
    el.innerHTML = '<table class="table"><thead><tr>' +
      '<th>ID</th><th>标题</th><th>难度</th><th>提交/通过率</th><th>操作</th>' +
      '</tr></thead><tbody>' +
      allProblems.map((p) =>
        '<tr>' +
        '<td>' + p.id + '</td>' +
        '<td>' + escapeHtml(p.title) + '</td>' +
        '<td>' + diffLabel(p.difficulty) + '</td>' +
        '<td>' + p.submit_count + ' / ' + p.pass_rate + '%</td>' +
        '<td>' +
        '<button class="btn btn-sm" data-edit="' + p.id + '" style="margin-right:6px;">修改</button>' +
        '<button class="btn btn-sm" data-pdel="' + p.id + '" style="background:var(--danger);">删除</button>' +
        '</td></tr>'
      ).join('') +
      '</tbody></table>';
  } catch (err) {
    const ael = document.getElementById('alert');
    showAlert(ael, 'error', err.message);
  }
}

// 修改：拉取详情 + 隐藏测试点，回填表单
async function startEdit(id) {
  const el = document.getElementById('alert');
  hideAlert(el);
  try {
    const [detail, cases] = await Promise.all([
      api('/api/problems/' + id),
      api('/api/admin/problems/' + id + '/testcases'),
    ]);
    const p = detail.data.problem;
    editingProblemId = id;
    document.getElementById('problem-form-title').textContent = '修改题目';
    document.getElementById('problem-edit-id').textContent = '正在编辑 #' + id;
    document.getElementById('prob-title').value = p.title;
    document.getElementById('prob-difficulty').value = String(p.difficulty);
    document.getElementById('prob-time').value = p.time_limit_ms;
    document.getElementById('prob-mem').value = p.memory_limit_mb;
    document.getElementById('prob-sample-in').value = p.sample_in;
    document.getElementById('prob-sample-out').value = p.sample_out;
    document.getElementById('prob-desc').value = p.description;
    const tcs = (cases.data && cases.data.testcases) || [];
    document.getElementById('case-editor').innerHTML = '';
    caseSeq = 0;
    tcs.forEach((t) => addCaseRow(t.input, t.output, t.score === null ? '' : t.score));
    if (tcs.length === 0) addCaseRow();
    document.getElementById('problem-import-btn').style.display = 'none';
    document.getElementById('problem-update-btn').style.display = 'inline-block';
    document.getElementById('problem-cancel-btn').style.display = 'inline-block';
    window.scrollTo(0, document.body.scrollHeight);
  } catch (err) {
    showAlert(el, 'error', err.message);
  }
}

function cancelEdit() {
  resetProblemForm();
}

async function submitProblem() {
  const el = document.getElementById('alert');
  hideAlert(el);
  const json = collectProblemJSON();
  const cases = collectCases();
  try {
    validateProblemForm(json, cases);
  } catch (err) {
    showAlert(el, 'error', err.message);
    return;
  }
  // 有测试用例行时才携带 test_cases（编辑元数据时避免误清隐藏测试点）
  if (cases.length > 0) {
    json.test_cases = cases.map((c) => {
      const o = { input: c.input, output: c.output };
      if (c.score !== null) o.score = c.score;
      return o;
    });
  }
  const isEdit = !!editingProblemId;
  try {
    const data = await api(
      isEdit ? '/api/admin/problems/' + editingProblemId : '/api/admin/problems/import',
      { method: isEdit ? 'PUT' : 'POST', body: json });
    showAlert(el, 'success',
      isEdit ? '题目已更新' : '题目已发布，ID=' + data.data.id);
    resetProblemForm();
    loadProblems();
  } catch (err) {
    showAlert(el, 'error', err.message);
  }
}

function handleProblemAction(btn) {
  const el = document.getElementById('alert');
  hideAlert(el);

  if (btn.dataset.edit) {
    startEdit(parseInt(btn.dataset.edit, 10));
    return;
  }

  const doDelete = () =>
    api('/api/admin/problems/' + btn.dataset.pdel, { method: 'DELETE' })
      .then(() => { showAlert(el, 'success', '题目已删除'); loadProblems(); })
      .catch((err) => showAlert(el, 'error', err.message));

  if (btn.dataset.pdel) {
    openConfirm({
      message: '确定删除该题目吗？其所有提交记录将一并删除，且不可恢复。',
      okText: '确认删除题目',
      onConfirm: doDelete,
    });
  }
}

// ── 教师/管理员：统计与 CSV 导出 ────────────────────────────
function fillSelectOptions(sel, items, idKey, labelKey) {
  const cur = sel.value;
  sel.innerHTML = '<option value="">全部</option>';
  items.forEach((it) => {
    const o = document.createElement('option');
    o.value = it[idKey];
    o.textContent = it[labelKey];
    sel.appendChild(o);
  });
  if (cur) sel.value = cur;
}

async function loadStats() {
  const el = document.getElementById('alert');
  hideAlert(el);
  try {
    const data = await api('/api/admin/stats');
    const s = data.data || {};
    document.getElementById('st-total').textContent = s.total_submit || 0;
    document.getElementById('st-ac').textContent = s.total_ac || 0;
    document.getElementById('st-rate').textContent = (s.total_rate || 0) + '%';

    const ps = s.problem_stats || [];
    const pbody = document.getElementById('stats-problem-body');
    pbody.innerHTML = '';
    ps.forEach((p) => {
      const tr = document.createElement('tr');
      tr.innerHTML = '<td>' + escapeHtml(p.title) + '</td>' +
        '<td>' + p.submit_count + '</td>' +
        '<td>' + p.ac_count + '</td>' +
        '<td>' + p.pass_rate + '%</td>';
      pbody.appendChild(tr);
    });
    document.getElementById('stats-problem-empty').style.display =
      ps.length ? 'none' : 'block';

    const ss = s.student_stats || [];
    const sbody = document.getElementById('stats-student-body');
    sbody.innerHTML = '';
    ss.forEach((u) => {
      const tr = document.createElement('tr');
      tr.innerHTML = '<td>' + escapeHtml(u.username) + '</td>' +
        '<td>' + u.submit_count + '</td>' +
        '<td>' + u.ac_count + '</td>' +
        '<td>' + u.pass_rate + '%</td>';
      sbody.appendChild(tr);
    });
    document.getElementById('stats-student-empty').style.display =
      ss.length ? 'none' : 'block';

    fillSelectOptions(document.getElementById('export-problem'), ps,
      'problem_id', 'title');
    fillSelectOptions(document.getElementById('export-user'), ss,
      'user_id', 'username');
  } catch (err) {
    showAlert(el, 'error', err.message);
  }
}

async function exportCsv() {
  const el = document.getElementById('alert');
  hideAlert(el);
  const params = new URLSearchParams();
  const pid = document.getElementById('export-problem').value;
  const uid = document.getElementById('export-user').value;
  if (pid) params.set('problem_id', pid);
  if (uid) params.set('user_id', uid);
  const qs = params.toString();
  const url = '/api/admin/submissions/export.csv' + (qs ? '?' + qs : '');
  try {
    const res = await fetch(url, { credentials: 'same-origin' });
    if (!res.ok) {
      let msg = '导出失败 (' + res.status + ')';
      try {
        const d = await res.json();
        if (d.error && d.error.message) msg = d.error.message;
      } catch (e) { /* 非 JSON 错误体 */ }
      throw new Error(msg);
    }
    const blob = await res.blob();
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'submissions.csv';
    document.body.appendChild(a);
    a.click();
    a.remove();
    URL.revokeObjectURL(a.href);
    showAlert(el, 'success', 'CSV 已导出');
  } catch (err) {
    showAlert(el, 'error', err.message);
  }
}

// ── 初始化 ──────────────────────────────────────────────────
document.addEventListener('DOMContentLoaded', () => {
  initPage('admin', true).then((user) => {
    if (!user) return;
    const isStaff = user.role === 'teacher' || user.role === 'admin';
    if (!isStaff) {
      window.location.href = '/pages/problems.html';
      return;
    }

    document.getElementById('problem-panel').style.display = 'block';
    document.getElementById('stats-panel').style.display = 'block';
    loadProblems();
    loadStats();
    resetProblemForm();

    if (user.role === 'teacher') {
      document.getElementById('class-panel').style.display = 'block';
      loadClass();
    } else {
      document.getElementById('user-panel').style.display = 'block';
      document.getElementById('config-panel').style.display = 'block';
      loadUsers();
      loadConfig();
    }
  });

  const classForm = document.getElementById('class-form');
  if (classForm) classForm.addEventListener('submit', createClass);
  const inviteBtn = document.getElementById('invite-btn');
  if (inviteBtn) inviteBtn.addEventListener('click', resetInvite);

  const userForm = document.getElementById('user-create-form');
  if (userForm) userForm.addEventListener('submit', createUser);
  const userBody = document.getElementById('user-body');
  if (userBody) {
    userBody.addEventListener('click', (e) => {
      const btn = e.target.closest('button');
      if (btn) handleUserAction(btn);
    });
  }

  const configForm = document.getElementById('config-form');
  if (configForm) configForm.addEventListener('submit', saveConfig);

  // 统计与导出
  const exportBtn = document.getElementById('export-btn');
  if (exportBtn) exportBtn.addEventListener('click', exportCsv);

  // 判题配置 → 题目表单（内联测试用例编辑）
  const caseAdd = document.getElementById('case-add');
  if (caseAdd) caseAdd.addEventListener('click', () => addCaseRow());
  const caseEditor = document.getElementById('case-editor');
  if (caseEditor) {
    caseEditor.addEventListener('click', (e) => {
      const btn = e.target.closest('button[data-case-del]');
      if (!btn) return;
      const row = btn.closest('[data-case-row]');
      if (row) row.remove();
    });
  }

  const problemList = document.getElementById('problem-list');
  if (problemList) {
    problemList.addEventListener('click', (e) => {
      const btn = e.target.closest('button');
      if (btn) handleProblemAction(btn);
    });
  }
  const importBtn = document.getElementById('problem-import-btn');
  if (importBtn) importBtn.addEventListener('click', submitProblem);
  const updateBtn = document.getElementById('problem-update-btn');
  if (updateBtn) updateBtn.addEventListener('click', submitProblem);
  const cancelBtn = document.getElementById('problem-cancel-btn');
  if (cancelBtn) cancelBtn.addEventListener('click', cancelEdit);
});
