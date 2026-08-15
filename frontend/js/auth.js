// auth.js — 登录/注册页逻辑（阶段 3）
'use strict';

const USERNAME_RE = /^[A-Za-z0-9_]+$/;
const MIN_USERNAME_LEN = 3;
const MAX_USERNAME_LEN = 64;
const MIN_PASSWORD_LEN = 6;
const MAX_PASSWORD_LEN = 128;

// 登录表单提交。
async function handleLogin(e) {
  e.preventDefault();
  const usernameEl = document.getElementById('username');
  const passwordEl = document.getElementById('password');
  const alertEl = document.getElementById('alert');
  hideAlert(alertEl);

  const username = usernameEl.value.trim();
  const password = passwordEl.value;
  if (!username || !password) {
    showAlert(alertEl, 'error', '请输入用户名和密码');
    return;
  }

  const btn = document.getElementById('submit-btn');
  btn.disabled = true;
  try {
    await api('/api/login', { method: 'POST', body: { username, password } });
    window.location.href = '/pages/problems.html';
  } catch (err) {
    showAlert(alertEl, 'error', err.message);
    btn.disabled = false;
  }
}

// 注册表单提交。
async function handleRegister(e) {
  e.preventDefault();
  const usernameEl = document.getElementById('username');
  const passwordEl = document.getElementById('password');
  const confirmEl = document.getElementById('confirm');
  const alertEl = document.getElementById('alert');
  hideAlert(alertEl);

  const username = usernameEl.value.trim();
  const password = passwordEl.value;
  const confirm = confirmEl.value;

  if (!username || !password || !confirm) {
    showAlert(alertEl, 'error', '请填写所有字段');
    return;
  }
  if (username.length < MIN_USERNAME_LEN || username.length > MAX_USERNAME_LEN) {
    showAlert(
      alertEl,
      'error',
      '用户名长度需在 ' + MIN_USERNAME_LEN + ' 到 ' + MAX_USERNAME_LEN + ' 个字符之间'
    );
    return;
  }
  if (!USERNAME_RE.test(username)) {
    showAlert(alertEl, 'error', '用户名只能包含字母、数字和下划线');
    return;
  }
  if (password.length < MIN_PASSWORD_LEN || password.length > MAX_PASSWORD_LEN) {
    showAlert(
      alertEl,
      'error',
      '密码长度需在 ' + MIN_PASSWORD_LEN + ' 到 ' + MAX_PASSWORD_LEN + ' 个字符之间'
    );
    return;
  }
  if (password !== confirm) {
    showAlert(alertEl, 'error', '两次输入的密码不一致');
    return;
  }

  const btn = document.getElementById('submit-btn');
  btn.disabled = true;
  try {
    const teacherCode = (document.getElementById('teacher-code').value || '').trim();
    const body = { username, password };
    if (teacherCode) body.teacher_code = teacherCode;
    await api('/api/register', { method: 'POST', body });
    window.location.href = '/pages/login.html?registered=1';
  } catch (err) {
    showAlert(alertEl, 'error', err.message);
    btn.disabled = false;
  }
}

document.addEventListener('DOMContentLoaded', () => {
  initNav('login');

  // 注册成功跳转后显示成功提示
  if (new URLSearchParams(window.location.search).get('registered') === '1') {
    const el = document.getElementById('alert');
    if (el) showAlert(el, 'success', '注册成功，请登录');
  }

  const loginForm = document.getElementById('login-form');
  if (loginForm) {
    loginForm.addEventListener('submit', handleLogin);
  }
  const registerForm = document.getElementById('register-form');
  if (registerForm) {
    registerForm.addEventListener('submit', handleRegister);
  }
});
