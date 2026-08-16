// index.js — 首页落地页：渲染导航 / 依据登录态调整 CTA / 加载平台数据
'use strict';

document.addEventListener('DOMContentLoaded', async () => {
  await initNav('home');

  const user = await currentUser();

  // 依据登录态调整 CTA 文案与跳转
  const primary = document.getElementById('hero-cta-primary');
  const secondary = document.getElementById('hero-cta-secondary');
  const ctaPrimary = document.getElementById('cta-primary');
  const ctaSecondary = document.getElementById('cta-secondary');

  if (user) {
    if (primary) {
      primary.textContent = '进入题库';
      primary.href = '/pages/problems.html';
    }
    if (secondary) {
      secondary.textContent = '查看我的统计';
      secondary.href = '/pages/stats.html';
    }
    if (ctaPrimary) {
      ctaPrimary.textContent = '开始刷题';
      ctaPrimary.href = '/pages/problems.html';
    }
    if (ctaSecondary) {
      ctaSecondary.textContent = '查看统计';
      ctaSecondary.href = '/pages/stats.html';
    }
  }

  // 加载平台数据条（题目数 / 提交 / 通过率）
  await loadLandingStats(user);
});

// 拉取题目列表汇总展示：有数据则显示统计条，否则保持隐藏。
async function loadLandingStats(user) {
  const box = document.getElementById('landing-stats');
  if (!box) return;

  let problems = [];
  try {
    const data = await api('/api/problems');
    problems = (data && data.data && data.data.problems) || [];
  } catch (e) {
    /* 拉取失败则不展示统计条 */
  }

  if (!problems.length) return;

  let submits = 0;
  let rateSum = 0;
  let rateN = 0;
  let ac = 0;
  for (const p of problems) {
    submits += p.submit_count || 0;
    if (typeof p.pass_rate === 'number') {
      rateSum += p.pass_rate;
      rateN += 1;
    }
    if (p.my_status === 'AC') ac += 1;
  }

  const setNum = (id, text) => {
    const el = document.getElementById(id);
    if (el) el.textContent = text;
  };

  setNum('stat-problems', String(problems.length));
  setNum('stat-submits', submits >= 1000 ? (submits / 1000).toFixed(1) + 'k' : String(submits));
  setNum('stat-rate', rateN ? Math.round(rateSum / rateN) + '%' : '—');
  setNum('stat-ac', user ? String(ac) : '—');

  box.hidden = false;
}
