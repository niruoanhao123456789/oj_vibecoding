// problems.js — 题目列表页逻辑（阶段 8：加入班级 + 题目列表）
'use strict';

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
  })[c]);
}

const DIFF = { 1: '简单', 2: '中等', 3: '困难' };

function diffLabel(d) {
  return DIFF[d] || '未知';
}

function statusBadge(s) {
  if (s === 'AC') return '<span class="badge badge-ac">AC</span>';
  if (s === 'attempted') return '<span class="badge badge-wa">尝试中</span>';
  if (s === 'not_started') return '<span class="badge badge-na">未作答</span>';
  return '<span class="muted">—</span>';
}

async function loadProblems() {
  const table = document.getElementById('problem-body');
  const empty = document.getElementById('empty-hint');
  table.innerHTML = '';
  try {
    const data = await api('/api/problems');
    const list = (data.data && data.data.problems) || [];
    if (list.length === 0) {
      empty.style.display = 'block';
      return;
    }
    empty.style.display = 'none';
    list.forEach((p) => {
      const tr = document.createElement('tr');
      tr.innerHTML =
        '<td>' + p.id + '</td>' +
        '<td><a href="/pages/problems.html?id=' + p.id + '">' +
        escapeHtml(p.title) + '</a></td>' +
        '<td>' + diffLabel(p.difficulty) + '</td>' +
        '<td>' + p.submit_count + ' / ' + p.pass_rate + '%</td>' +
        '<td>' + statusBadge(p.my_status) + '</td>';
      table.appendChild(tr);
    });
  } catch (err) {
    const el = document.getElementById('alert');
    showAlert(el, 'error', err.message);
  }
}

async function loadMyClass() {
  const el = document.getElementById('my-class');
  if (!el) return;
  try {
    const data = await api('/api/me');
    const user = data.data;
    // 学生尝试展示已加入班级：这里通过题目可见性间接体现，不额外查询。
    el.innerHTML = '<p class="muted">当前登录：' +
      escapeHtml(user.username) + '（' + escapeHtml(user.role) + '）</p>';
  } catch (e) { /* 未登录 */ }
}

async function handleJoin(e) {
  e.preventDefault();
  const el = document.getElementById('alert');
  hideAlert(el);
  const code = document.getElementById('invite-code').value.trim();
  if (!code) {
    showAlert(el, 'error', '请输入邀请码');
    return;
  }
  const btn = document.getElementById('join-btn');
  btn.disabled = true;
  try {
    await api('/api/class/join', { method: 'POST', body: { invite_code: code } });
    showAlert(el, 'success', '加入班级成功');
    document.getElementById('invite-code').value = '';
    loadProblems();
  } catch (err) {
    showAlert(el, 'error', err.message);
  } finally {
    btn.disabled = false;
  }
}

document.addEventListener('DOMContentLoaded', () => {
  initPage('problems', true).then((user) => {
    loadProblems();
    loadMyClass();
  });
  const form = document.getElementById('join-form');
  if (form) form.addEventListener('submit', handleJoin);
});
