// problem.js — 题目详情页逻辑（阶段 7：代码编辑器 + 提交 + 轮询结果；自测运行）
'use strict';

let problemId = null;
let runCases = [];        // 自测用例 [{ input, expected }]
let runVerdicts = [];     // 上次运行的逐例 verdict（与 runCases 对齐，用于徽标）

// ── 简易 C/C++ 语法高亮 ──────────────────────────────────────
// 按行高亮：注释 / 字符串 / 数字 / 关键字 / 预处理指令。
function highlightLine(line) {
  let s = escapeHtml(line);
  // 行注释
  if (/^\s*\/\//.test(s)) {
    return '<span style="color:#6a9955">' + s + '</span>';
  }
  // 预处理指令（#include 等）
  s = s.replace(
    /(#\s*\w+.*)$/,
    '<span style="color:#c678dd">$1</span>'
  );
  // 字符串
  s = s.replace(
    /(&quot;.*?&quot;|&#39;.*?&#39;)/g,
    '<span style="color:#98c379">$1</span>'
  );
  // 数字
  s = s.replace(
    /\b(\d+)\b/g,
    '<span style="color:#d19a66">$1</span>'
  );
  // 关键字
  const kw = '\\b(for|while|if|else|return|int|char|long|short|void|struct|class|namespace|using|include|define|const|static|unsigned|signed|double|float|bool|true|false|new|delete|break|continue|switch|case|default|main|std|printf|scanf|cout|cin|endl)\\b';
  s = s.replace(
    new RegExp(kw, 'g'),
    '<span style="color:#61afef">$1</span>'
  );
  return s;
}

function renderEditor() {
  const ta = document.getElementById('code-editor');
  const nums = document.getElementById('line-numbers');
  const hl = document.getElementById('editor-highlight');
  const lines = ta.value.split('\n');

  nums.innerHTML = lines
    .map((_, i) => '<div>' + (i + 1) + '</div>')
    .join('');
  hl.innerHTML = lines
    .map((l) => highlightLine(l))
    .join('\n') + '\n';
  updateCursorLine();
}

// ── 编辑器增强（光标行高亮 / Tab 缩进 / 括号引号补全）────────

const OPEN_PAIRS = { '(': ')', '[': ']', '{': '}', '"': '"', "'": "'" };

// 当前光标所在行号（0 起）。
function cursorLine(ta) {
  return ta.value.slice(0, ta.selectionStart).split('\n').length - 1;
}

// 刷新光标行高亮位置（跟随滚动）。
function updateCursorLine() {
  const ta = document.getElementById('code-editor');
  const line = document.getElementById('cursor-line');
  if (!ta || !line) return;
  const lh = parseFloat(getComputedStyle(ta).lineHeight) || 20;
  line.style.top = (10 + cursorLine(ta) * lh - ta.scrollTop) + 'px';
  line.style.height = lh + 'px';
  line.style.display = 'block';
}

// 设置光标位置并滚动到可见区域。
function setCaret(ta, pos) {
  ta.focus();
  ta.setSelectionRange(pos, pos);
}

// 在光标处插入文本（替换选区），返回插入后光标位置。
function insertAt(ta, text) {
  const start = ta.selectionStart;
  const end = ta.selectionEnd;
  ta.value = ta.value.slice(0, start) + text + ta.value.slice(end);
  ta.setSelectionRange(start + text.length, start + text.length);
  renderEditor();
  return start + text.length;
}

// Tab：无选区插入 4 空格；多行选区每行行首缩进 4 空格。
function indentTab(ta) {
  const start = ta.selectionStart;
  const end = ta.selectionEnd;
  const firstLine = ta.value.slice(0, start).split('\n').length - 1;
  const lastLine = ta.value.slice(0, end).split('\n').length - 1;

  if (firstLine === lastLine) {
    insertAt(ta, '    ');
    return;
  }

  const lines = ta.value.split('\n');
  for (let i = firstLine; i <= lastLine; i++) {
    lines[i] = '    ' + lines[i];
  }
  const selOffset = lastLine - firstLine;
  ta.value = lines.join('\n');
  ta.setSelectionRange(
    start + 4,
    end + 4 * (selOffset + 1)
  );
  renderEditor();
}

// 光标前一个字符是否为单词字符（用于引号补全判断）。
function prevIsWordChar(ta) {
  const pos = ta.selectionStart;
  if (pos === 0) return false;
  return /[A-Za-z0-9_]/.test(ta.value.charAt(pos - 1));
}

// 光标右侧是否恰好是给定字符。
function nextIsChar(ta, ch) {
  return ta.value.charAt(ta.selectionStart) === ch;
}

// 光标是否位于空成对符号中间，例如 "|" 处两字符为 "(|)"。
function caretInsideEmptyPair(ta) {
  const pos = ta.selectionStart;
  if (pos === 0 || pos >= ta.value.length) return false;
  const left = ta.value.charAt(pos - 1);
  const right = ta.value.charAt(pos);
  return OPEN_PAIRS[left] === right;
}

// keydown 处理：Tab 缩进、括号/引号补全、右符跳过、空对退格。
function handleEditorKeydown(e) {
  const ta = document.getElementById('code-editor');
  if (!ta) return;
  const key = e.key;

  if (key === 'Tab') {
    e.preventDefault();
    indentTab(ta);
    return;
  }

  // 成对开启符：插入配对并置光标中间
  if (OPEN_PAIRS[key]) {
    // 引号在单词字符旁不补全（避免标识符中间误补）
    if ((key === '"' || key === "'") && prevIsWordChar(ta)) {
      return; // 保留默认输入
    }
    // 若已有选区，用成对包裹选区
    if (ta.selectionStart !== ta.selectionEnd) {
      e.preventDefault();
      const s = ta.selectionStart;
      const t = ta.selectionEnd;
      const sel = ta.value.slice(s, t);
      ta.value = ta.value.slice(0, s) + key + sel + OPEN_PAIRS[key] +
        ta.value.slice(t);
      ta.setSelectionRange(s + 1, t + 1);
      renderEditor();
      return;
    }
    e.preventDefault();
    const pos = insertAt(ta, key + OPEN_PAIRS[key]);
    ta.setSelectionRange(pos - 1, pos - 1);
    return;
  }

  // 成对关闭符：若下一个字符正好是它，直接跳过
  if (key === ')' || key === ']' || key === '}' || key === '"' ||
      key === "'") {
    if (nextIsChar(ta, key)) {
      e.preventDefault();
      setCaret(ta, ta.selectionStart + 1);
      updateCursorLine();
      return;
    }
  }

  // Backspace：空成对中间一次删除两个
  if (key === 'Backspace' && caretInsideEmptyPair(ta)) {
    e.preventDefault();
    const pos = ta.selectionStart;
    ta.value = ta.value.slice(0, pos - 1) + ta.value.slice(pos + 1);
    ta.setSelectionRange(pos - 1, pos - 1);
    renderEditor();
  }
}

// ── 加载题目详情 ─────────────────────────────────────────────
async function loadProblem() {
  try {
    const data = await api('/api/problems/' + problemId);
    const p = data.data.problem;
    document.getElementById('problem-title').textContent =
      '#' + p.id + ' ' + p.title;
    document.getElementById('problem-meta').innerHTML =
      '<span>难度：' + diffLabel(p.difficulty) + '</span>' +
      '<span>时限：' + p.time_limit_ms + ' ms</span>' +
      '<span>内存：' + p.memory_limit_mb + ' MB</span>';
    document.getElementById('problem-desc').textContent = p.description;
    document.getElementById('sample-in').textContent = p.sample_in || '';
    document.getElementById('sample-out').textContent = p.sample_out || '';

    // 自测运行：默认以题目明文样例为用例（用户可增删改）
    runCases = [{ input: p.sample_in || '', expected: p.sample_out || '' }];
    runVerdicts = [null];
    renderRunCases();

    document.getElementById('loading').style.display = 'none';
    document.getElementById('problem-content').style.display = 'grid';
    renderEditor();
  } catch (err) {
    const el = document.getElementById('alert');
    showAlert(el, 'error', err.message);
    document.getElementById('loading').innerHTML =
      '<p class="muted">题目加载失败。</p>';
  }
}

// ── 提交与轮询 ───────────────────────────────────────────────
async function handleSubmit() {
  const el = document.getElementById('alert');
  hideAlert(el);
  const code = document.getElementById('code-editor').value;
  if (!code.trim()) {
    showAlert(el, 'error', '代码不能为空');
    return;
  }
  if (code.length > 100 * 1024) {
    showAlert(el, 'error', '代码过长（超过 100KB）');
    return;
  }

  const btn = document.getElementById('submit-btn');
  btn.disabled = true;
  btn.textContent = '提交中…';
  const resultBox = document.getElementById('result-box');
  resultBox.style.display = 'none';

  let sid = null;
  try {
    const data = await api('/api/submissions', {
      method: 'POST',
      body: {
        problem_id: problemId,
        language: document.getElementById('lang-select').value,
        code,
      },
    });
    sid = data.data.id;
  } catch (err) {
    showAlert(el, 'error', err.message);
    btn.disabled = false;
    btn.textContent = '提交';
    return;
  }

  // 立即展示初始状态并开始轮询
  resultBox.style.display = 'block';
  renderResult({ status: 'PENDING', exec_time_ms: null, memory_kb: null });
  renderResultError('');
  btn.textContent = '判题中…';

  await pollSubmission(sid, renderResult, 1200, 180000);
  renderResult(await fetchSubmission(sid));
  btn.disabled = false;
  btn.textContent = '提交';
}

async function fetchSubmission(sid) {
  try {
    const data = await api('/api/submissions/' + sid);
    return data.data.submission;
  } catch (e) {
    return null;
  }
}

// 更新结果区显示（WA 时展示首个失败点详情）。
function renderResult(sub) {
  if (!sub) return;
  document.getElementById('result-status').innerHTML = statusBadge(sub.status);
  document.getElementById('result-time').textContent = formatTime(sub.exec_time_ms);
  document.getElementById('result-memory').textContent = formatMem(sub.memory_kb);
  renderResultError(sub.error_message);

  const link = document.getElementById('result-link');
  link.style.display = 'block';
  link.setAttribute('href', '/pages/submission.html?id=' + sub.id);
}

function renderResultError(msg) {
  const box = document.getElementById('result-error');
  if (msg && String(msg).trim()) {
    box.textContent = String(msg);
    box.style.display = 'block';
  } else {
    box.style.display = 'none';
  }
}

// ── 自测运行（运行不算正式提交）──────────────────────────────
function renderRunCases() {
  const box = document.getElementById('run-cases');
  if (!box) return;
  box.innerHTML = '';
  if (runCases.length === 0) {
    box.innerHTML =
      '<p class="muted" style="font-size:13px;">暂无自测用例，点击下方按钮添加。</p>';
    return;
  }
  runCases.forEach((c, i) => {
    const row = document.createElement('div');
    row.className = 'run-case';
    row.innerHTML =
      '<div class="run-case-field"><label>输入</label>' +
      '<textarea class="run-case-input" rows="3"></textarea></div>' +
      '<div class="run-case-field"><label>期望输出（可留空，仅显示实际输出）</label>' +
      '<textarea class="run-case-expected" rows="3"></textarea></div>' +
      '<div class="run-case-verdict"></div>' +
      '<div class="run-case-del"><button type="button" class="btn btn-sm" ' +
      'data-del style="background:var(--danger);">删除</button></div>';
    row.querySelector('.run-case-input').value = c.input;
    row.querySelector('.run-case-expected').value = c.expected || '';
    if (runVerdicts[i]) {
      row.querySelector('.run-case-verdict').innerHTML =
        c.expected.trim() ? statusBadge(runVerdicts[i]) :
          '<span class="badge badge-na">' + escapeHtml(runVerdicts[i]) + '</span>';
    }
    box.appendChild(row);
  });
}

function addRunCase() {
  runCases.push({ input: '', expected: '' });
  runVerdicts.push(null);
  renderRunCases();
}

function deleteRunCase(row) {
  const box = document.getElementById('run-cases');
  const i = Array.prototype.indexOf.call(box.children, row);
  if (i < 0) return;
  runCases.splice(i, 1);
  runVerdicts.splice(i, 1);
  renderRunCases();
}

// 用例编辑：实时同步状态并清除该行旧的运行结果
function onRunCaseEdit(e) {
  const ta = e.target.closest('textarea');
  if (!ta) return;
  const row = ta.closest('.run-case');
  const box = document.getElementById('run-cases');
  const i = Array.prototype.indexOf.call(box.children, row);
  if (i < 0) return;
  if (ta.classList.contains('run-case-input')) {
    runCases[i].input = ta.value;
  } else {
    runCases[i].expected = ta.value;
  }
  runVerdicts[i] = null;
  row.querySelector('.run-case-verdict').innerHTML = '';
}

async function handleRun() {
  const el = document.getElementById('alert');
  hideAlert(el);
  const code = document.getElementById('code-editor').value;
  if (!code.trim()) {
    showAlert(el, 'error', '代码不能为空');
    return;
  }
  if (code.length > 100 * 1024) {
    showAlert(el, 'error', '代码过长（超过 100KB）');
    return;
  }
  const language = document.getElementById('lang-select').value;
  const testCases = runCases.map((c) => ({ input: c.input, expected: c.expected }));
  const btn = document.getElementById('run-btn');
  const box = document.getElementById('run-result');
  btn.disabled = true;
  btn.textContent = '运行中…';
  box.style.display = 'none';
  try {
    const data = await api('/api/problems/' + problemId + '/run', {
      method: 'POST',
      body: { language, code, test_cases: testCases },
    });
    renderRunResult(data.data);
  } catch (err) {
    showAlert(el, 'error', err.message);
  } finally {
    btn.disabled = false;
    btn.textContent = '运行';
  }
}

function renderRunResult(d) {
  const box = document.getElementById('run-result');
  const compile = d.compile || {};
  runVerdicts = (d.cases || []).map((c) => c.verdict);

  // 编译失败：直接展示编译输出
  if (!compile.ok) {
    const status = compile.timed_out ? 'COMPILE_TIMEOUT' : 'COMPILE_ERROR';
    box.style.display = 'block';
    box.innerHTML =
      '<div class="run-result-summary">运行结果：' + statusBadge(status) +
      '<span class="muted">（不提交、不计入提交记录）</span></div>' +
      (compile.output
        ? '<div class="run-error">' + escapeHtml(compile.output) + '</div>'
        : '<p class="muted">编译失败（无输出）。</p>');
    renderRunCases();
    return;
  }

  const cases = d.cases || [];
  const rows = cases.map((c) =>
    '<tr>' +
    '<td>' + c.num + '</td>' +
    '<td>' + (c.verdict === 'NONE'
      ? '<span class="badge badge-na">无期望输出</span>'
      : statusBadge(c.verdict)) + '</td>' +
    '<td>' + formatTime(c.time_ms) + '</td>' +
    '<td>' + formatMem(c.memory_kb) + '</td>' +
    '<td><pre class="run-out">' + escapeHtml(c.actual) + '</pre></td>' +
    '<td>' + (c.expected === ''
      ? '<span class="muted">—</span>'
      : '<pre class="run-out">' + escapeHtml(c.expected) + '</pre>') + '</td>' +
    '<td>' + (c.error
      ? '<span class="run-error" style="display:block;margin:0;">' + escapeHtml(c.error) + '</span>'
      : '') + '</td>' +
    '</tr>'
  ).join('');

  const summary = d.overall === 'NONE'
    ? '<span class="muted">未判定（全部用例未提供期望输出）</span>'
    : '运行结果：' + statusBadge(d.overall);

  box.style.display = 'block';
  box.innerHTML =
    '<div class="run-result-summary">' + summary +
    '<span class="muted">（不提交、不计入提交记录）</span></div>' +
    (rows
      ? '<table class="run-result-table"><thead><tr>' +
        '<th>#</th><th>结果</th><th>耗时</th><th>内存</th>' +
        '<th>实际输出</th><th>期望输出</th><th>错误信息</th>' +
        '</tr></thead><tbody>' + rows + '</tbody></table>'
      : '<p class="muted">无自测用例。</p>');
  renderRunCases();
}

document.addEventListener('DOMContentLoaded', () => {
  initPage('problems', true).then((user) => {
    const params = new URLSearchParams(window.location.search);
    const id = params.get('id');
    if (!id || !/^\d+$/.test(id)) {
      window.location.href = '/pages/problems.html';
      return;
    }
    problemId = parseInt(id, 10);
    loadProblem();
  });

  const ta = document.getElementById('code-editor');
  if (ta) {
    ta.addEventListener('input', renderEditor);
    ta.addEventListener('keydown', handleEditorKeydown);
    ta.addEventListener('scroll', () => {
      document.getElementById('line-numbers').scrollTop = ta.scrollTop;
      document.getElementById('editor-highlight').scrollTop = ta.scrollTop;
      updateCursorLine();
    });
    ['keyup', 'click', 'mouseup', 'select', 'focus'].forEach((ev) => {
      ta.addEventListener(ev, updateCursorLine);
    });
  }
  const btn = document.getElementById('submit-btn');
  if (btn) btn.addEventListener('click', handleSubmit);

  // 自测运行
  const runBtn = document.getElementById('run-btn');
  if (runBtn) runBtn.addEventListener('click', handleRun);
  const runAdd = document.getElementById('run-add-case');
  if (runAdd) runAdd.addEventListener('click', addRunCase);
  const runCasesBox = document.getElementById('run-cases');
  if (runCasesBox) {
    runCasesBox.addEventListener('input', onRunCaseEdit);
    runCasesBox.addEventListener('click', (e) => {
      const del = e.target.closest('button[data-del]');
      if (del) deleteRunCase(del.closest('.run-case'));
    });
  }
});
