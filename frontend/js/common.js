// common.js — fetch 封装 / 登录态 / 导航渲染 / 状态徽标 / 轮询工具（阶段 3 / 7）
'use strict';

// 统一 fetch 封装：
//   - 自动携带 Cookie（same-origin）
//   - 对象入参自动序列化为 JSON
//   - 统一 JSON 响应解析；业务失败（ok:false 或非 2xx）抛出带 code/status 的错误
async function api(path, options = {}) {
  const opts = Object.assign({ credentials: 'same-origin' }, options);
  if (opts.body && typeof opts.body !== 'string') {
    opts.headers = Object.assign({}, opts.headers, {
      'Content-Type': 'application/json',
    });
    opts.body = JSON.stringify(opts.body);
  }
  const res = await fetch(path, opts);
  let data = null;
  const text = await res.text();
  if (text) {
    try {
      data = JSON.parse(text);
    } catch (e) {
      data = null;
    }
  }
  if (!res.ok) {
    const err = new Error(
      (data && data.error && data.error.message) || '请求失败 (' + res.status + ')'
    );
    err.status = res.status;
    err.code = data && data.error && data.error.code;
    err.data = data;
    throw err;
  }
  return data;
}

// 获取当前登录用户；未登录返回 null。
async function currentUser() {
  try {
    const data = await api('/api/me');
    return data.data || null;
  } catch (e) {
    return null;
  }
}

// 登出：调用接口后跳转登录页。
async function logout() {
  try {
    await api('/api/logout', { method: 'POST' });
  } catch (e) {
    /* 忽略登出失败 */
  }
  window.location.href = '/pages/login.html';
}

// 渲染公共导航栏（含登录态）。active 为当前页面标识（可选）。
async function initNav(active) {
  const nav = document.getElementById('main-nav');
  if (!nav) return;

  const links = [];
  if (active !== 'login' && active !== 'register') {
    links.push('<a href="/pages/problems.html" data-page="problems">题目</a>');
    links.push('<a href="/pages/submissions.html" data-page="submissions">提交记录</a>');
    links.push('<a href="/pages/stats.html" data-page="stats">我的统计</a>');
  }

  nav.innerHTML =
    '<a class="brand" href="/">OJ Vibecoding</a>' +
    '<div class="nav-links">' +
    links.join('') +
    '<span id="nav-auth"></span>' +
    '</div>';

  const user = await currentUser();
  const auth = document.getElementById('nav-auth');
  if (user) {
    let adminLink = '';
    if (user.role === 'teacher' || user.role === 'admin') {
      adminLink = '<a href="/pages/admin.html" data-page="admin">管理</a>';
    }
    auth.innerHTML =
      adminLink +
      '<span class="nav-user">' +
      escapeHtml(user.username) +
      ' (' +
      escapeHtml(user.role) +
      ')</span>' +
      '<a href="#" id="nav-logout">登出</a>';
    const btn = document.getElementById('nav-logout');
    btn.addEventListener('click', (e) => {
      e.preventDefault();
      logout();
    });
  } else {
    auth.innerHTML =
      '<a href="/pages/login.html">登录</a>' +
      '<a href="/pages/register.html">注册</a>';
  }

  if (active) {
    const el = document.querySelector('.nav-links a[data-page="' + active + '"]');
    if (el) el.classList.add('active');
  }
}

// HTML 转义，防止用户名等注入。
function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({
    '&': '&amp;',
    '<': '&lt;',
    '>': '&gt;',
    '"': '&quot;',
    "'": '&#39;',
  })[c]);
}

// 展示/隐藏提示框。
function showAlert(el, type, message) {
  if (!el) return;
  el.textContent = message;
  el.className = 'alert alert-' + type;
  el.style.display = 'block';
}

function hideAlert(el) {
  if (el) el.style.display = 'none';
}

// 初始化页面：渲染导航；若页面要求登录而用户未登录则跳转登录页。
async function initPage(active, requireLogin) {
  await initNav(active);
  if (requireLogin) {
    const user = await currentUser();
    if (!user) {
      window.location.href = '/pages/login.html';
      return null;
    }
    return user;
  }
  return null;
}

// ── 判题状态徽标与工具（阶段 7）─────────────────────────────────────────

// 判题状态 → 徽标样式类映射。
const STATUS_CLASS = {
  PENDING: 'badge-pending',
  COMPILING: 'badge-pending',
  RUNNING: 'badge-pending',
  COMPILE_ERROR: 'badge-wa',
  COMPILE_TIMEOUT: 'badge-wa',
  AC: 'badge-ac',
  WA: 'badge-wa',
  RE: 'badge-wa',
  TLE: 'badge-wa',
  MLE: 'badge-wa',
  SYSTEM_ERROR: 'badge-sys',
};

// 判题状态 → 中文显示名。
const STATUS_TEXT = {
  PENDING: '排队中',
  COMPILING: '编译中',
  RUNNING: '判题中',
  COMPILE_ERROR: '编译错误',
  COMPILE_TIMEOUT: '编译超时',
  AC: '通过',
  WA: '答案错误',
  RE: '运行错误',
  TLE: '超时',
  MLE: '超内存',
  SYSTEM_ERROR: '系统错误',
};

// 终态集合（轮询终止条件）。
const TERMINAL_STATUS = new Set([
  'AC', 'WA', 'RE', 'TLE', 'MLE',
  'COMPILE_ERROR', 'COMPILE_TIMEOUT', 'SYSTEM_ERROR',
]);

// 渲染判题状态徽标 HTML。
function statusBadge(status) {
  const text = STATUS_TEXT[status] || status || '未知';
  const cls = STATUS_CLASS[status] || 'badge-na';
  return '<span class="badge ' + cls + '">' + escapeHtml(text) + '</span>';
}

// 延时工具。
function sleep(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

// 轮询提交状态：每 interval 毫秒查询一次，直到进入终态或超时。
// 每次查询后回调 onUpdate(submission)；返回最终 submission（超时返回 null）。
async function pollSubmission(id, onUpdate, interval = 1200, timeoutMs = 180000) {
  const deadline = Date.now() + timeoutMs;
  for (;;) {
    let sub = null;
    try {
      const data = await api('/api/submissions/' + id);
      sub = data.data && data.data.submission;
    } catch (e) {
      /* 网络抖动时继续重试 */
    }
    if (sub) {
      onUpdate(sub);
      if (TERMINAL_STATUS.has(sub.status)) return sub;
    }
    if (Date.now() >= deadline) return null;
    await sleep(interval);
  }
}

// 格式化字节数为易读大小（KB/MB）。
function formatMem(kb) {
  if (kb == null || kb === '') return '—';
  if (kb >= 1024) return (kb / 1024).toFixed(2) + ' MB';
  return kb + ' KB';
}

// 格式化耗时为易读字符串。
function formatTime(ms) {
  if (ms == null || ms === '') return '—';
  return ms + ' ms';
}

// 难度 → 中文。
const DIFF_TEXT = { 1: '简单', 2: '中等', 3: '困难' };
function diffLabel(d) {
  return DIFF_TEXT[d] || '未知';
}
