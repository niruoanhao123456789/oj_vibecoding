// stats.js — 个人统计页逻辑（阶段 7：由 GET /api/submissions 聚合计算）
'use strict';

// 从提交列表聚合个人统计：
//   提交总数 / AC 数 / 通过率 / 作答题目数 / 各题状态 / 按天分布。
function computeStats(list) {
  const total = list.length;
  let ac = 0;

  // 各题聚合：提交数、AC 数、最佳状态
  const byProblem = new Map();
  // 按天分布
  const byDay = new Map();

  list.forEach((s) => {
    if (s.status === 'AC') ac++;

    const pid = s.problem_id;
    if (!byProblem.has(pid)) {
      byProblem.set(pid, {
        id: pid,
        title: s.problem_title || ('#' + pid),
        count: 0,
        ac: 0,
        best: 'not_started',
      });
    }
    const p = byProblem.get(pid);
    p.count++;
    if (s.status === 'AC') {
      p.ac++;
      p.best = 'AC';
    } else if (p.best !== 'AC') {
      p.best = 'attempted';
    }

    const day = (s.created_at || '').slice(0, 10);
    if (day) {
      if (!byDay.has(day)) byDay.set(day, { count: 0, ac: 0 });
      const d = byDay.get(day);
      d.count++;
      if (s.status === 'AC') d.ac++;
    }
  });

  return {
    total,
    ac,
    rate: total > 0 ? Math.round((ac * 100) / total) : 0,
    problems: byProblem.size,
    problemList: Array.from(byProblem.values()),
    dayList: Array.from(byDay.entries()).sort((a, b) =>
      a[0] < b[0] ? -1 : 1
    ),
  };
}

function bestBadge(best) {
  if (best === 'AC') return '<span class="badge badge-ac">AC</span>';
  if (best === 'attempted') return '<span class="badge badge-wa">尝试中</span>';
  return '<span class="badge badge-na">未作答</span>';
}

async function loadStats() {
  try {
    const data = await api('/api/submissions');
    const list = (data.data && data.data.submissions) || [];
    const st = computeStats(list);

    document.getElementById('loading').style.display = 'none';
    document.getElementById('stats-content').style.display = 'block';

    document.getElementById('stat-total').textContent = st.total;
    document.getElementById('stat-ac').textContent = st.ac;
    document.getElementById('stat-rate').textContent = st.rate + '%';
    document.getElementById('stat-problems').textContent = st.problems;

    const pbody = document.getElementById('problem-stats-body');
    const pempty = document.getElementById('problem-stats-empty');
    pbody.innerHTML = '';
    if (st.problemList.length === 0) {
      pempty.style.display = 'block';
    } else {
      pempty.style.display = 'none';
      st.problemList.forEach((p) => {
        const tr = document.createElement('tr');
        tr.innerHTML =
          '<td><a href="/pages/problem.html?id=' + p.id + '">' +
          escapeHtml(p.title) + '</a></td>' +
          '<td>' + p.count + '</td>' +
          '<td>' + p.ac + '</td>' +
          '<td>' + bestBadge(p.best) + '</td>';
        pbody.appendChild(tr);
      });
    }

    const dbody = document.getElementById('daily-body');
    const dempty = document.getElementById('daily-empty');
    dbody.innerHTML = '';
    if (st.dayList.length === 0) {
      dempty.style.display = 'block';
    } else {
      dempty.style.display = 'none';
      st.dayList.forEach(([day, d]) => {
        const tr = document.createElement('tr');
        tr.innerHTML =
          '<td>' + escapeHtml(day) + '</td>' +
          '<td>' + d.count + '</td>' +
          '<td>' + d.ac + '</td>';
        dbody.appendChild(tr);
      });
    }
  } catch (err) {
    const el = document.getElementById('alert');
    showAlert(el, 'error', err.message);
    document.getElementById('loading').textContent = '统计加载失败。';
  }
}

document.addEventListener('DOMContentLoaded', () => {
  initPage('stats', true).then((user) => {
    loadStats();
  });
});
