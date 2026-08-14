// admin.js — 管理端逻辑（阶段 8：教师班级管理）
'use strict';

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
  })[c]);
}

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

document.addEventListener('DOMContentLoaded', () => {
  initPage('admin', true).then((user) => {
    if (!user) return;
    if (user.role !== 'teacher' && user.role !== 'admin') {
      window.location.href = '/pages/problems.html';
      return;
    }
    loadClass();
  });

  const form = document.getElementById('class-form');
  if (form) form.addEventListener('submit', createClass);
  const inviteBtn = document.getElementById('invite-btn');
  if (inviteBtn) inviteBtn.addEventListener('click', resetInvite);
});
