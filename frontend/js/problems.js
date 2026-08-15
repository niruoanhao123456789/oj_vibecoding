// problems.js — 题目列表页逻辑（阶段 7：筛选/搜索/状态徽标 + 阶段 8：加入班级）
'use strict';

// 题目列表原始数据缓存（供筛选/搜索）
let allProblems = [];

function statusBadge(s) {
  if (s === 'AC') return '<span class="badge badge-ac">AC</span>';
  if (s === 'attempted') return '<span class="badge badge-wa">尝试中</span>';
  if (s === 'not_started') return '<span class="badge badge-na">未作答</span>';
  return '<span class="muted">—</span>';
}

function applyFilters() {
  const diff = document.getElementById('diff-filter').value;
  const kw = document.getElementById('search-input').value.trim().toLowerCase();

  let list = allProblems;
  if (diff !== 'all') {
    list = list.filter((p) => String(p.difficulty) === diff);
  }
  if (kw) {
    list = list.filter((p) => String(p.title).toLowerCase().includes(kw));
  }
  renderTable(list);
}

function renderTable(list) {
  const table = document.getElementById('problem-body');
  const empty = document.getElementById('empty-hint');
  table.innerHTML = '';
  if (list.length === 0) {
    empty.style.display = 'block';
    empty.textContent =
      allProblems.length === 0
        ? '暂无可见题目。加入教师班级后可见本班题目。'
        : '没有符合条件的题目。';
    return;
  }
  empty.style.display = 'none';
  list.forEach((p) => {
    const tr = document.createElement('tr');
    tr.innerHTML =
      '<td>' + p.id + '</td>' +
      '<td><a href="/pages/problem.html?id=' + p.id + '">' +
      escapeHtml(p.title) + '</a></td>' +
      '<td>' + diffLabel(p.difficulty) + '</td>' +
      '<td>' + p.submit_count + ' / ' + p.pass_rate + '%</td>' +
      '<td>' + statusBadge(p.my_status) + '</td>';
    table.appendChild(tr);
  });
}

async function loadProblems() {
  try {
    const data = await api('/api/problems');
    allProblems = (data.data && data.data.problems) || [];
    applyFilters();
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
  const diffSel = document.getElementById('diff-filter');
  if (diffSel) diffSel.addEventListener('change', applyFilters);
  const searchInput = document.getElementById('search-input');
  if (searchInput) {
    searchInput.addEventListener('input', applyFilters);
  }
});
