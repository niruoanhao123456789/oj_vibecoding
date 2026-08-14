// common.js — fetch 封装 / 登录态 / 导航渲染（阶段 3）
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
