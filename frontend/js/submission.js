// submission.js — 提交详情页逻辑（阶段 7：代码 + 状态时间线 + 失败点详情）
'use strict';

const LANG_TEXT = { cpp: 'C++', c: 'C' };

// 状态机时间线顺序（终态着色）。
const FLOW = [
  { status: 'PENDING', label: '排队中' },
  { status: 'COMPILING', label: '编译' },
  { status: 'RUNNING', label: '运行' },
];

const FINAL_STEPS = [
  { status: 'AC', label: '通过', failed: false },
  { status: 'WA', label: '答案错误', failed: true },
  { status: 'RE', label: '运行错误', failed: true },
  { status: 'TLE', label: '超时', failed: true },
  { status: 'MLE', label: '超内存', failed: true },
  { status: 'COMPILE_ERROR', label: '编译错误', failed: true },
  { status: 'COMPILE_TIMEOUT', label: '编译超时', failed: true },
  { status: 'SYSTEM_ERROR', label: '系统错误', failed: true },
];

// 渲染状态时间线：已走过的步骤标记 done / failed，当前步骤标记 current。
function renderTimeline(status) {
  const el = document.getElementById('timeline');
  const steps = [];

  for (const step of FLOW) {
    let cls = 'timeline-step';
    if (status === step.status) cls += ' current';
    else if (stepIndex(status) > stepIndex(step.status)) cls += ' done';
    steps.push('<span class="' + cls + '">' + step.label + '</span>');
  }
  steps.push('<span class="timeline-step">→</span>');

  for (const fin of FINAL_STEPS) {
    let cls = 'timeline-step';
    if (status === fin.status) cls += fin.failed ? ' failed' : ' done';
    steps.push('<span class="' + cls + '">' + fin.label + '</span>');
  }
  el.innerHTML = steps.join('');
}

// 返回状态在 FLOW 中的下标；不在此列时返回 -1。
function stepIndex(status) {
  for (let i = 0; i < FLOW.length; i++) {
    if (FLOW[i].status === status) return i;
  }
  return -1;
}

async function loadSubmission() {
  const params = new URLSearchParams(window.location.search);
  const id = params.get('id');
  if (!id || !/^\d+$/.test(id)) {
    window.location.href = '/pages/submissions.html';
    return;
  }

  try {
    const data = await api('/api/submissions/' + id);
    const s = data.data.submission;

    document.getElementById('loading').style.display = 'none';
    document.getElementById('sub-content').style.display = 'block';

    document.getElementById('sub-meta').innerHTML =
      '<span><a href="/pages/problem.html?id=' + s.problem_id + '">' +
      escapeHtml(s.problem_title || ('#' + s.problem_id)) + '</a></span>' +
      '<span>语言：' + (LANG_TEXT[s.language] || s.language) + '</span>' +
      '<span>提交 ID：' + s.id + '</span>' +
      '<span>时间：' + escapeHtml(s.created_at || '') + '</span>';

    renderTimeline(s.status);
    document.getElementById('result-status').innerHTML = statusBadge(s.status);
    document.getElementById('result-time').textContent = formatTime(s.exec_time_ms);
    document.getElementById('result-memory').textContent = formatMem(s.memory_kb);

    const errBox = document.getElementById('result-error');
    if (s.error_message && String(s.error_message).trim()) {
      errBox.textContent = String(s.error_message);
      errBox.style.display = 'block';
    } else {
      errBox.style.display = 'none';
    }

    document.getElementById('sub-code').textContent = s.code || '';
  } catch (err) {
    const el = document.getElementById('alert');
    showAlert(el, 'error', err.message);
    document.getElementById('loading').textContent = '提交加载失败。';
  }
}

document.addEventListener('DOMContentLoaded', () => {
  initPage('submissions', true).then((user) => {
    loadSubmission();
  });
});
