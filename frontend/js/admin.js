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

// ── 教师/管理员：题目管理 ───────────────────────────────────
let allProblems = [];
let editingProblemId = null;

async function loadProblems() {
  const el = document.getElementById('problem-list');
  try {
    const data = await api('/api/problems');
    allProblems = (data.data && data.data.problems) || [];
    fillJudgeProblemSelect();
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

// 从当前用户可见的题目里取元数据（不含测试点）
async function startEdit(id) {
  const el = document.getElementById('alert');
  hideAlert(el);
  try {
    const data = await api('/api/problems/' + id);
    const p = data.data.problem;
    editingProblemId = id;
    const json = {
      title: p.title,
      description: p.description,
      sample_in: p.sample_in,
      sample_out: p.sample_out,
      time_limit_ms: p.time_limit_ms,
      memory_limit_mb: p.memory_limit_mb,
      difficulty: p.difficulty,
    };
    document.getElementById('problem-json').value = JSON.stringify(json, null, 2);
    document.getElementById('problem-edit-id').textContent =
      '正在编辑 #' + id + '（可补充 test_cases 以替换测试点）';
    document.getElementById('problem-update-btn').style.display = 'inline-block';
    document.getElementById('problem-cancel-btn').style.display = 'inline-block';
    document.getElementById('problem-import-btn').style.display = 'none';
    window.scrollTo(0, document.body.scrollHeight);
  } catch (err) {
    showAlert(el, 'error', err.message);
  }
}

function cancelEdit() {
  editingProblemId = null;
  document.getElementById('problem-json').value = '';
  document.getElementById('problem-edit-id').textContent = '';
  document.getElementById('problem-update-btn').style.display = 'none';
  document.getElementById('problem-cancel-btn').style.display = 'none';
  document.getElementById('problem-import-btn').style.display = 'inline-block';
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

async function importProblem() {
  const el = document.getElementById('alert');
  hideAlert(el);
  let root = null;
  try {
    root = JSON.parse(document.getElementById('problem-json').value);
  } catch (e) {
    showAlert(el, 'error', 'JSON 格式错误：' + e.message);
    return;
  }
  if (!root || typeof root !== 'object' || Array.isArray(root)) {
    showAlert(el, 'error', 'JSON 必须是一个对象');
    return;
  }
  try {
    const data = await api('/api/admin/problems/import', {
      method: 'POST',
      body: root,
    });
    showAlert(el, 'success', '题目已导入，ID=' + data.data.id);
    document.getElementById('problem-json').value = '';
    loadProblems();
  } catch (err) {
    showAlert(el, 'error', err.message);
  }
}

async function updateProblem() {
  const el = document.getElementById('alert');
  hideAlert(el);
  let root = null;
  try {
    root = JSON.parse(document.getElementById('problem-json').value);
  } catch (e) {
    showAlert(el, 'error', 'JSON 格式错误：' + e.message);
    return;
  }
  try {
    await api('/api/admin/problems/' + editingProblemId, {
      method: 'PUT',
      body: root,
    });
    showAlert(el, 'success', '题目已更新');
    cancelEdit();
    loadProblems();
  } catch (err) {
    showAlert(el, 'error', err.message);
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

// ── 教师/管理员：判题配置（限制与测试用例维护）─────────────
let judgeProblemId = null;

function fillJudgeProblemSelect() {
  const sel = document.getElementById('judge-problem');
  if (!sel) return;
  const cur = judgeProblemId;
  sel.innerHTML = '<option value="">选择题目…</option>';
  allProblems.forEach((p) => {
    const o = document.createElement('option');
    o.value = p.id;
    o.textContent = '#' + p.id + ' ' + p.title;
    sel.appendChild(o);
  });
  if (cur) sel.value = cur;
}

async function onJudgeProblemChange() {
  const el = document.getElementById('alert');
  hideAlert(el);
  const pid = parseInt(document.getElementById('judge-problem').value, 10);
  judgeProblemId = pid || null;
  document.getElementById('limit-time').value = '';
  document.getElementById('limit-mem').value = '';
  document.getElementById('testcase-list').innerHTML = '';
  if (!judgeProblemId) return;
  try {
    const data = await api('/api/problems/' + pid);
    const p = data.data.problem;
    document.getElementById('limit-time').value = p.time_limit_ms;
    document.getElementById('limit-mem').value = p.memory_limit_mb;
    await loadTestCases();
  } catch (err) {
    showAlert(el, 'error', err.message);
  }
}

async function saveLimits() {
  const el = document.getElementById('alert');
  hideAlert(el);
  if (!judgeProblemId) {
    showAlert(el, 'error', '请先选择题目');
    return;
  }
  const timeVal = document.getElementById('limit-time').value;
  const memVal = document.getElementById('limit-mem').value;
  const time = parseInt(timeVal, 10);
  const mem = parseInt(memVal, 10);
  if (!timeVal || !memVal || isNaN(time) || isNaN(mem) || time <= 0 || mem <= 0) {
    showAlert(el, 'error', '时间/内存限制必须为正整数');
    return;
  }
  try {
    await api('/api/admin/problems/' + judgeProblemId + '/limits', {
      method: 'PUT',
      body: { time_limit_ms: time, memory_limit_mb: mem },
    });
    showAlert(el, 'success', '判题限制已保存');
  } catch (err) {
    showAlert(el, 'error', err.message);
  }
}

async function loadTestCases() {
  const el = document.getElementById('alert');
  const box = document.getElementById('testcase-list');
  if (!judgeProblemId) return;
  try {
    const data = await api('/api/admin/problems/' + judgeProblemId + '/testcases');
    const tcs = (data.data && data.data.testcases) || [];
    if (tcs.length === 0) {
      box.innerHTML = '<p class="muted">暂无测试用例。</p>';
      return;
    }
    const preview = (s) =>
      '<pre style="max-width:280px;white-space:pre-wrap;word-break:break-all;' +
      'margin:0;font-family:monospace;font-size:12px;line-height:1.3;">' +
      escapeHtml(s) + '</pre>';
    box.innerHTML = '<table class="table"><thead><tr>' +
      '<th>#</th><th>输入预览</th><th>期望输出预览</th><th>分值</th><th>操作</th>' +
      '</tr></thead><tbody>' +
      tcs.map((t) =>
        '<tr>' +
        '<td>' + t.num + '</td>' +
        '<td>' + preview(t.input) + '</td>' +
        '<td>' + preview(t.output) + '</td>' +
        '<td>' + (t.score === null ? '—' : escapeHtml(String(t.score))) + '</td>' +
        '<td><button class="btn btn-sm" data-tcdel="' + t.num +
        '" style="background:var(--danger);">删除</button></td>' +
        '</tr>'
      ).join('') +
      '</tbody></table>';
  } catch (err) {
    showAlert(el, 'error', err.message);
  }
}

async function addTestCase() {
  const el = document.getElementById('alert');
  hideAlert(el);
  if (!judgeProblemId) {
    showAlert(el, 'error', '请先选择题目');
    return;
  }
  const input = document.getElementById('tc-input').value;
  const output = document.getElementById('tc-output').value;
  if (!input && !output) {
    showAlert(el, 'error', '请填写输入与期望输出');
    return;
  }
  const body = { input, output };
  const scoreVal = document.getElementById('tc-score').value;
  if (scoreVal !== '') {
    const score = parseInt(scoreVal, 10);
    if (isNaN(score)) {
      showAlert(el, 'error', '分值必须为整数');
      return;
    }
    body.score = score;
  }
  try {
    await api('/api/admin/problems/' + judgeProblemId + '/testcases', {
      method: 'POST',
      body,
    });
    showAlert(el, 'success', '测试点已添加');
    document.getElementById('tc-input').value = '';
    document.getElementById('tc-output').value = '';
    document.getElementById('tc-score').value = '';
    loadTestCases();
  } catch (err) {
    showAlert(el, 'error', err.message);
  }
}

function deleteTestCase(num) {
  const el = document.getElementById('alert');
  hideAlert(el);
  if (!judgeProblemId) return;
  openConfirm({
    message: '确定删除测试点 #' + num + ' 吗？删除后后续编号将前移。',
    warning: '此操作会删除该测试点的 .in/.out 文件，请确认。',
    okText: '确认删除',
    onConfirm: () => {
      api('/api/admin/problems/' + judgeProblemId + '/testcases/' + num,
        { method: 'DELETE' })
        .then(() => {
          showAlert(el, 'success', '测试点已删除');
          loadTestCases();
        })
        .catch((err) => showAlert(el, 'error', err.message));
    },
  });
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
    document.getElementById('judge-panel').style.display = 'block';
    loadProblems();
    loadStats();

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

  // 判题配置
  const judgeProblem = document.getElementById('judge-problem');
  if (judgeProblem) {
    judgeProblem.addEventListener('change', onJudgeProblemChange);
  }
  const limitSave = document.getElementById('limit-save');
  if (limitSave) limitSave.addEventListener('click', saveLimits);
  const tcAdd = document.getElementById('tc-add');
  if (tcAdd) tcAdd.addEventListener('click', addTestCase);
  const tcList = document.getElementById('testcase-list');
  if (tcList) {
    tcList.addEventListener('click', (e) => {
      const btn = e.target.closest('button[data-tcdel]');
      if (btn) deleteTestCase(parseInt(btn.dataset.tcdel, 10));
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
  if (importBtn) importBtn.addEventListener('click', importProblem);
  const updateBtn = document.getElementById('problem-update-btn');
  if (updateBtn) updateBtn.addEventListener('click', updateProblem);
  const cancelBtn = document.getElementById('problem-cancel-btn');
  if (cancelBtn) cancelBtn.addEventListener('click', cancelEdit);
});
