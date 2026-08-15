// submissions.js — 提交历史页逻辑（阶段 7：列表 + 按题筛选）
'use strict';

let allSubmissions = [];

const LANG_TEXT = { cpp: 'C++', c: 'C' };
function langLabel(l) {
  return LANG_TEXT[l] || l || '—';
}

function renderTable(list) {
  const tbody = document.getElementById('submission-body');
  const empty = document.getElementById('empty-hint');
  const hint = document.getElementById('count-hint');
  tbody.innerHTML = '';
  hint.textContent = '共 ' + list.length + ' 条';

  if (list.length === 0) {
    empty.style.display = 'block';
    return;
  }
  empty.style.display = 'none';
  list.forEach((s) => {
    const tr = document.createElement('tr');
    tr.innerHTML =
      '<td><a href="/pages/submission.html?id=' + s.id + '">' + s.id + '</a></td>' +
      '<td><a href="/pages/problem.html?id=' + s.problem_id + '">' +
      escapeHtml(s.problem_title || ('#' + s.problem_id)) + '</a></td>' +
      '<td>' + langLabel(s.language) + '</td>' +
      '<td>' + statusBadge(s.status) + '</td>' +
      '<td>' + formatTime(s.exec_time_ms) + '</td>' +
      '<td>' + formatMem(s.memory_kb) + '</td>' +
      '<td>' + escapeHtml(s.created_at || '') + '</td>';
    tbody.appendChild(tr);
  });
}

function applyFilter() {
  const sel = document.getElementById('problem-filter');
  const pid = sel.value;
  const list = pid === 'all'
    ? allSubmissions
    : allSubmissions.filter((s) => String(s.problem_id) === pid);
  renderTable(list);
}

async function loadSubmissions() {
  try {
    const data = await api('/api/submissions');
    allSubmissions = (data.data && data.data.submissions) || [];

    // 构建题目筛选下拉（按题目去重，保留顺序）
    const sel = document.getElementById('problem-filter');
    const seen = new Set();
    allSubmissions.forEach((s) => {
      if (seen.has(s.problem_id)) return;
      seen.add(s.problem_id);
      const opt = document.createElement('option');
      opt.value = String(s.problem_id);
      opt.textContent = '#' + s.problem_id + ' ' + (s.problem_title || '');
      sel.appendChild(opt);
    });

    applyFilter();
  } catch (err) {
    const el = document.getElementById('alert');
    showAlert(el, 'error', err.message);
  }
}

document.addEventListener('DOMContentLoaded', () => {
  initPage('submissions', true).then((user) => {
    loadSubmissions();
  });
  const sel = document.getElementById('problem-filter');
  if (sel) sel.addEventListener('change', applyFilter);
});
