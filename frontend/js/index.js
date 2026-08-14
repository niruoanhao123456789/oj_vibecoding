// index.js — 入口页：根据登录态跳转
'use strict';

document.addEventListener('DOMContentLoaded', async () => {
  const user = await currentUser();
  window.location.href = user ? '/pages/problems.html' : '/pages/login.html';
});
