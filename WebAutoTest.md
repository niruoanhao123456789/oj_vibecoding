# WebAutoTest.md — 仿 LeetCode 教学 OJ 系统 Web 自动化测试用例设计

> 依据：`SPEC.md`（功能点/业务规则/页面描述）+ `frontend/pages/*.html` 与 `frontend/js/*.js`（实际 DOM 结构与交互逻辑）+ `API.md`（接口契约）。
> 技术栈：**Python 3 + pytest + Playwright**（chromium/webkit/firefox）。
> 被测环境：**公网服务器** `http://x.x.xxx.xxx:8080`（`oj_server` 监听 `0.0.0.0:8080`，已探活通过 `GET /api/health` → `200`）。`x.x.xxx.xxx` 为占位符，**测试者需将其替换为自己的公网服务器 IP 地址**（或通过环境变量 `OJ_WEB_BASE` 指定）。

---

## 1. 测试概述

### 1.1 目的
通过浏览器端自动化用例，对 OJ 平台从「注册 → 登录 → 选题 → 提交 → 判题 → 历史 → 统计 → 管理端」全链路进行 UI 级回归验证，覆盖 SPEC 第 3、7、8、9、11 章描述的功能点与权限边界，同时验证前端各页面真实渲染与交互（编辑器增强、自测运行、轮询状态、确认弹窗等）。

### 1.2 范围
| 覆盖模块 | 对应 SPEC | 对应前端文件 |
|---|---|---|
| 落地首页 / 公共导航 | 6、7 | `index.html`、`common.js`、`index.js` |
| 注册 / 登录 / 登出 | 7.1、7.2 | `login.html`、`register.html`、`auth.js` |
| 题目列表 / 筛选 / 搜索 / 加入班级 | 4.9、7.3 | `problems.html`、`problems.js` |
| 题目详情 / 编辑器增强 / 自测 / 提交 | 7.4 | `problem.html`、`problem.js` |
| 提交历史 / 提交详情 | 7.5、7.6 | `submissions.html`、`submission.html` 及对应 js |
| 个人统计 | 7.7 | `stats.html`、`stats.js` |
| 教师管理端（班级/题目/统计/CSV） | 7.8、阶段 8 | `admin.html`、`admin.js` |
| 管理员管理端（用户/配置/全局题/保护规则） | 7.8、4.10、阶段 8 | `admin.html`、`admin.js` |
| 权限与边界（403/可见性/非法输入） | 3、9、11 | 全部页面 |

### 1.3 非范围
- 后端接口/判题引擎逻辑的纯单元验证（见 `tests/api/*.sh` 与 `tests/unit/*`）。
- 性能/并发（30 人并发、2-4 worker 压测）仅作人工验收，不纳入本 UI 用例。
- 浏览器兼容性全矩阵（仅覆盖 chromium 主基线，其余内核可复用同用例）。

### 1.4 读者须知（零基础快速上手）

> 本小节面向**对本项目完全不了解**的测试人员（例如在 Windows 机器上首次接手）。花 3 分钟读完，即可按本文档独立搭建环境并执行用例。

**这是什么系统？**
一个仿 LeetCode 的在线判题（OJ）教学平台：学生在网页上浏览题目、编写 C/C++ 代码、提交后由后端异步判题，实时看到「通过 / 答案错误 / 超时」等结果；教师建班发题、看统计；管理员管用户与系统配置。你只需要一个浏览器和 Python 环境，无需搭建服务器、无需懂 C++ 或数据库。

**三种角色（测试中会用到）：**

| 角色 | 是什么 | 在本文档中的用途 |
|---|---|---|
| 管理员 | 系统最高权限，管理用户/全局题/系统配置 | 前置造数与清理（§3）、管理端用例（§5.11）、权限用例（§5.12） |
| 教师 | 建班、发题、看统计 | 管理端用例（§5.10）、注册用例（§5.2） |
| 学生 | 入班、刷题、提交 | 全链路用例主体（§5.4–5.9） |

**如何使用本文档：**
1. 先读 §2「测试环境与配置」——确认服务器地址、账号（§2.4）、Windows 安装步骤（§2.5）。
2. 再读 §3「前置准备」——用例运行前要先造好教师/班级/题目/学生数据（自动化用例会按此处设计自行准备）。
3. 最后按 §5 用例表逐条执行：每条都写清了前置条件、操作步骤（含页面元素 ID）与预期结果。
4. 用例编号含义与优先级见 §4；SPEC 章节追踪见 §6；执行与报告见 §7。

---

## 2. 测试环境与配置

### 2.1 被测服务
| 项 | 值 |
|---|---|
| 公网访问地址 | `http://x.x.xxx.xxx:8080`（占位符，需替换为**你自己的**公网服务器 IP，见 §2.4） |
| 服务进程 | `./build/oj_server config/server.json`（cpp-httplib，端口 8080，2 worker） |
| 数据库 | MySQL（单机，见 `config/server.json`） |
| 探活接口 | `GET http://x.x.xxx.xxx:8080/api/health` → `{"status":"ok"}` |

### 2.2 自动化框架配置
```python
# conftest.py（设计示例）
import os
import time
import pytest
from playwright.sync_api import sync_playwright

BASE_URL = os.environ.get("OJ_WEB_BASE", "http://x.x.xxx.xxx:8080")
# 预置管理员凭据，见 §2.4；若管理员密码被修改则用环境变量覆盖
ADMIN_USER = os.environ.get("OJ_ADMIN_USER", "admin")
ADMIN_PASS = os.environ.get("OJ_ADMIN_PASS", "admin123")
# 教师注册邀请码，见 §2.4；若被管理员修改则用环境变量覆盖
TEACHER_CODE = os.environ.get("OJ_TEACHER_CODE", "TEACH-2026")

@pytest.fixture(scope="session")
def browser():
    with sync_playwright() as p:
        yield p.chromium.launch(headless=True)

@pytest.fixture
def page(browser):
    ctx = browser.new_context()
    page = ctx.new_page()
    yield page
    ctx.close()

@pytest.fixture
def unique_user():
    ts = str(int(time.time() * 1000))
    return f"ui_{ts}"
```

依赖安装：`pip install pytest playwright && playwright install chromium`

### 2.3 测试数据策略（公网共享服务器注意事项）
- **唯一性**：所有注册用户、题目标题均使用时间戳后缀（如 `ui_169xxxxxxxx_stu`），避免与存量数据/并行运行冲突。
- **数据清理**：用例结尾通过管理员接口删除测试用户（含班级级联）与测试题目；清理失败记录 WARN 不阻塞结果。
- **预置数据**：默认使用内建管理员 `admin / admin123`（凭据、来源与覆盖方式见 §2.4），若密码被改则通过环境变量 `OJ_ADMIN_PASS` 覆盖。
- **初始题目**：运行 `oj_db_reset`（见下）后存在 **8 道全局种子题**（`created_by NULL`，未入班学生登录即可见），覆盖三种难度（难度 1×3、难度 2×3、难度 3×2），含 TLE/MLE 探针题：

  | 标题 | 难度 | 特殊点 | 用途 |
  |---|---|---|---|
  | A+B Problem | 1 | 3 测试点（test_dir） | AC/WA、宽松比较 |
  | Greeting | 1 | 2 测试点 + score | 计分测试点 |
  | 无限循环探针 | 1 | `time_limit_ms=100` | TLE 用例（死循环必超时） |
  | 三数最大值 | 2 | 3 测试点（内联） | AC/WA 多测试点 |
  | 字符串反转 | 2 | 3 测试点（test_dir） | test_dir 导入覆盖 |
  | 内存上限探针 | 2 | `memory_limit_mb=16` | MLE 用例（大量分配必超内存） |
  | 斐波那契数列 | 3 | 3 测试点 + score | 难度 3、计分测试点 |
  | 素数判断 | 3 | 4 测试点（test_dir） | 难度 3、test_dir 覆盖 |

  故前置用例无需再造 A+B/超时/超内存题，直接使用种子题即可。种子题由 `./build/oj_db_reset --config config/server.json --yes` 一键生成（幂等，`--seed-only` 可单独重跑）；执行重置会清空全部业务数据，故「未登录 → 空列表」类断言应放在运行 `oj_db_reset` 之后、其它用例造数之前验证。
- **判题异步**：提交后依赖轮询（前端 `pollSubmission`，间隔 1200ms，超时 180s）。自动化中 `expect(locator).to_have_text(...)` 使用 Playwright 自动重试即可，必要时手动轮询 `GET /api/submissions/:id` 至终态（终态集合见 `common.js` `TERMINAL_STATUS`）。

### 2.4 预置账号与凭据（测试必备）

> 本小节为全文档唯一权威凭据来源，§2.2、§3 及用例均引用此处。凭据来自项目 `sql/schema.sql` 初始数据；公网服务器为共享环境，**运行前请先按 §2.5 或浏览器手动校验**，若被改动用环境变量覆盖。

| 项 | 默认值 | 来源/位置 | 用途 | 覆盖方式 |
|---|---|---|---|---|
| 公网地址 | `http://x.x.xxx.xxx:8080` | 本文档 §2.1 | 被测系统入口 | 环境变量 `OJ_WEB_BASE` |
| 管理员用户名 | `admin` | `sql/schema.sql`（`users` 表初始账号） | 前置造数/清理、管理员管理端用例、权限用例 | 环境变量 `OJ_ADMIN_USER` |
| 管理员密码 | `admin123` | `sql/schema.sql:116-119`（以 `时间戳盐:sha256(密码+盐)` 加密存储，明文为 admin123） | 同上 | 环境变量 `OJ_ADMIN_PASS` |
| 教师注册邀请码 | `TEACH-2026` | `sql/schema.sql:111`（`config` 表 `teacher_invite_code`） | 注册教师（§5.2）、教师造数（§3 P2） | 环境变量 `OJ_TEACHER_CODE`；管理员可在网页「管理 → 系统配置」查看/修改 |

**使用提示**
- 管理员登录入口：`/pages/login.html`，输入 `admin / admin123` 即可进入管理端（含用户管理、系统配置、全局题发布）。
- 教师邀请码修改路径：管理员登录 → 「管理」→「系统配置」→ `#config-code` 输入框 → 保存。
- 若 `admin` 登录失败：说明管理员密码已被修改，请向服务器维护者索取，或通过环境变量 `OJ_ADMIN_PASS` 注入后再运行测试。
- 公共服务器上的其他测试用户/题目可能与本文档冲突，故所有**自动化自建账号与题目一律使用时间戳唯一命名**（见 §2.3），用例结束后清理（见 §3 P5）。

**快速校验（任选其一）**
- 浏览器访问 `http://x.x.xxx.xxx:8080/api/health`，应返回 `{"status":"ok"}`。
- 浏览器访问 `/pages/login.html`，用 `admin / admin123` 登录，应进入题目列表页且导航栏显示 `admin (admin)` 与「管理」链接。

### 2.5 Windows 环境准备（从零配置）

> 本小节面向 Windows 上首次配置的测试人员，从装 Python 到跑起第一条用例全流程。

1. **安装 Python 3.10+**
   - 从官网（`https://www.python.org/downloads/`）下载安装包。
   - 安装时务必勾选 **Add python.exe to PATH**（否则命令行找不到 `python`）。
   - 验证：打开 CMD 或 PowerShell，执行 `python --version`，应输出版本号（如 `Python 3.12.x`）。
2. **安装测试框架**
   - 在 CMD/PowerShell 中执行：
     ```powershell
     python -m pip install --upgrade pip
     python -m pip install pytest playwright pytest-html
     ```
   - 下载浏览器内核（仅需 chromium 即可）：
     ```powershell
     python -m playwright install chromium
     ```
3. **准备测试目录**
   - 在 Windows 上新建目录，例如 `C:\oj_webtest\`，将本文档的 `conftest.py`（§2.2）与用例代码放入其中。
4. **运行用例**
   - 默认指向公网服务器。**先确定你自己的公网服务器 IP 地址**，通过环境变量 `OJ_WEB_BASE` 指定（或直接修改 `conftest.py` 中的默认值），再运行：
     ```powershell
     cd C:\oj_webtest
     python -m pytest -q
     ```
   - 若需更换服务器地址或管理员密码：
     ```powershell
     $env:OJ_WEB_BASE="http://x.x.xxx.xxx:8080"
     $env:OJ_ADMIN_PASS="admin123"
     python -m pytest -q
     ```
   - 生成 HTML 测试报告：
     ```powershell
     python -m pytest -q --html=report.html
     ```
   - 带界面（非无头）人工演示模式：在 `conftest.py` 中把 `headless=True` 改为 `headless=False`，或另加命令行参数。
5. **常见问题排查**
   | 现象 | 处理 |
   |---|---|
   | `python` 不是内部或外部命令 | 重装 Python 并勾选 Add to PATH，然后重新打开终端 |
   | `ModuleNotFoundError: playwright` | 重新执行 `python -m pip install playwright` |
   | 启动用例报缺少浏览器 | 重新执行 `python -m playwright install chromium` |
   | 用例大量 401 / 提示未登录 | 服务器地址或会话异常，先按 §2.4 快速校验 `admin` 登录 |
   | 用例失败且提示「登录已失效」 | 公网共享环境凭据/数据被改动，按 §2.4 校验并注入环境变量 |

---

## 3. 前置准备（Fixture 级测试数据）

> 以下为用例集运行前的「造数 + 清理」钩子设计，涉及接口调用通过同一浏览器会话或独立 API client 完成。

| 步骤 | 操作 | 说明 |
|---|---|---|
| P1 | 管理员 `admin` 登录（凭据见 §2.4），进入「管理」→「系统配置」，读取并保存当前教师邀请码（默认 `TEACH-2026`） | 用例 T-ADM-XXX 依赖 |
| P2 | 注册教师 `ui_{ts}_tch`（注册页填写教师邀请码）→ 登录 → 管理端创建班级 `UI自动班{ts}` → 记录班级邀请码 | 供学生入班 |
| P3 | 8 道全局种子题由 `oj_db_reset` 提供（难度 1/2/3 各 ≥2 题，含 TLE/MLE 探针，见 §2.3）。教师可另发 1 道本班题 `UI A+B {ts}`（2 个测试点）用于验证「本班题可见性」，如需可再发 1 道难度 3 本班题 | 供 AC/WA/TLE/难度筛选/可见性用例 |
| P4 | 注册学生 `ui_{ts}_stu`（不留邀请码）→ 登录 → 凭 P2 邀请码加入班级 | 供提交类用例 |
| P5 | 用例结束清理：管理员删除 P4 学生、P2 教师（级联删班级）、P3 题目；恢复 P1 教师邀请码 | 保持公网环境干净 |

> 造数也可直接改用接口完成（复用 `tests/api` 的 curl 模式），UI 用例专注于浏览器交互；本文用例默认按「接口造数 + UI 断言」与「纯 UI 造数」混合描述，每个用例均标注。

---

## 4. 用例编号规则与优先级

- 编号：`WT-<模块>-<序号>`
  - `AUTH` 注册/登录/登出
  - `PROB` 题目列表/详情/编辑器
  - `RUN` 自测运行
  - `SUB` 提交与判题轮询
  - `HIST` 提交历史/详情
  - `STAT` 个人统计
  - `TEACH` 教师管理端
  - `ADM` 管理员管理端
  - `PERM` 权限与可见性
  - `EDGE` 边界与异常
- 优先级：`P0` 核心链路（验收标准，必测） / `P1` 重要功能 / `P2` 一般/边界。
- 每用例含：前置条件 / 步骤（含 DOM 定位器）/ 预期结果 / 对应 SPEC 章节。

---

## 5. 测试用例

### 5.1 落地首页与公共导航

| 用例 | 前置 | 步骤 | 预期结果 |
|---|---|---|---|
| **WT-AUTH-001**（P1）落地页加载 | 无 | 访问 `GET /`；断言 `<nav id="main-nav">` 存在、品牌链接 `.brand` 文本「OJ Vibecoding」、主 CTA `#hero-cta-primary` 文本「立即注册」 | 页面 200，渲染完成，无 JS 报错；`#landing-stats` 初始为隐藏 |
| **WT-AUTH-002**（P1）未登录导航 | 未登录 | 访问首页，断言 `#nav-auth` 内容 | 显示「登录」「注册」两个链接，无用户名、无「登出」 |
| **WT-AUTH-003**（P2）已登录导航与 CTA | 已登录学生 | 访问首页 | `#nav-auth` 显示 `用户名(student)` 与「登出」链接；`#hero-cta-primary` 文案变「进入题库」且 href 指向 `/pages/problems.html`；导航含「题目」「提交记录」「我的统计」 |
| **WT-AUTH-004**（P2）教师/管理员导航 | 已登录教师 | 访问首页 | `#nav-auth` 额外含「管理」链接（`/pages/admin.html`） |

### 5.2 注册（对应 SPEC 7.2、阶段 3）

| 用例 | 前置 | 步骤 | 预期结果 |
|---|---|---|---|
| **WT-AUTH-005**（P0）学生注册成功 | 唯一用户名 `ui_{ts}_stu` | `/pages/register.html`：填 `#username`、`#password`、`#confirm`（一致），留空 `#teacher-code`，点 `#submit-btn` | 跳转 `/pages/login.html?registered=1`，`#alert` 显示「注册成功，请登录」；随后可用该账号登录 |
| **WT-AUTH-006**（P0）教师注册成功 | 唯一用户名 | 注册页填写正确教师邀请码 `TEACH-2026`（可从 `GET /api/admin/config` 读取） | 跳转登录页；登录后导航含「管理」链接 |
| **WT-AUTH-007**（P0）教师邀请码错误 | — | 注册页填写错误邀请码（如 `WRONG-CODE`） | 停留在注册页，`#alert` 错误提示「教师邀请码无效」（对应 `TEACHER_CODE_INVALID`） |
| **WT-AUTH-008**（P1）两次密码不一致 | — | `#password`=`abc123`，`#confirm`=`abc124` | 前端拦截，`#alert` 显示「两次输入的密码不一致」，不发起请求 |
| **WT-AUTH-009**（P1）用户名格式/长度校验 | — | 用户名 `ab`（<3）、`a!b`（含特殊字符）、密码 `123`（<6）分别提交 | `#alert` 依次显示「用户名长度需在 3 到 64 个字符之间」「用户名只能包含字母、数字和下划线」「密码长度需在 6 到 128 个字符之间」 |
| **WT-AUTH-010**（P1）必填校验 | — | 直接点提交（全空） | `#alert`「请填写所有字段」 |
| **WT-AUTH-011**（P1）用户名已存在 | 先注册 `ui_{ts}_dup` | 再次用同名注册 | `#alert` 显示「用户名已存在」（409） |
| **WT-AUTH-012**（P2）注册成功跳转登录页可登录 | 完成注册 | 跳转后登录页填写刚注册账号 | 登录成功跳转题目列表 |

### 5.3 登录 / 登出（对应 SPEC 7.1、API 3.2/3.3）

| 用例 | 前置 | 步骤 | 预期结果 |
|---|---|---|---|
| **WT-AUTH-013**（P0）登录成功 | 已注册学生 | `/pages/login.html` 填 `#username`/`#password` 点 `#submit-btn` | 跳转 `/pages/problems.html`；导航显示用户名 |
| **WT-AUTH-014**（P0）密码错误 | 已注册用户 | 填错误密码 | 停留在登录页，`#alert` 显示「密码错误」（401 `WRONG_PASSWORD`） |
| **WT-AUTH-015**（P1）用户名不存在 | — | 填随机用户名 | `#alert` 显示「用户名不存在」（401 `USER_NOT_FOUND`） |
| **WT-AUTH-016**（P1）空输入 | — | 直接提交空表单 | `#alert`「请输入用户名和密码」 |
| **WT-AUTH-017**（P1）登出 | 已登录 | 点导航 `#nav-logout` | 调 `POST /api/logout` 后跳转 `/pages/login.html`；返回题目列表页被重定向回登录页（需登录页面拦截） |
| **WT-AUTH-018**（P1）禁用账号登录 | 管理员将某学生 `status=0` | 该账号登录 | `#alert`「账号已被禁用」（403 `ACCOUNT_DISABLED`） |
| **WT-AUTH-019**（P2）回车提交 | 已注册 | 输入框内按 Enter | 与点按钮等效，正常登录跳转 |

### 5.4 题目列表 / 筛选 / 搜索 / 状态徽标 / 加入班级（对应 SPEC 4.9、7.3）

> 前置：按 §3 造数，教师题 `UI A+B {ts}`；全局题直接使用 §2.3 的 8 道种子题（`created_by NULL`），无需管理员再发布。

| 用例 | 前置 | 步骤 | 预期结果 |
|---|---|---|---|
| **WT-PROB-001**（P0）学生可见本班题与全局题 | 学生已入班 | 登录学生访问 `/pages/problems.html` | `#problem-body` 含教师题与全局题标题；表格列含 `# / 标题 / 难度 / 提交 / 通过率 / 我的状态` |
| **WT-PROB-002**（P0）未入班学生不可见教师题 | 学生 B 未入班 | 登录学生 B 访问题目列表 | 可见全局题；**不可见** `UI A+B {ts}` 教师题 |
| **WT-PROB-003**（P0）未登录题目列表为空 | 未登录 | 直接访问 `/pages/problems.html` | 被重定向到登录页（`initPage requireLogin`） |
| **WT-PROB-004**（P1）难度筛选 | 种子题含难度 1/2/3（见 §2.3） | 选择 `#diff-filter=1` | 列表仅剩难度「简单」题目；切回「全部难度」恢复；再分别选 `=2`、`=3`，列表分别只剩「中等」「困难」题目 |
| **WT-PROB-005**（P1）标题搜索 | 若干题目 | `#search-input` 输入关键字（如 `A+B`） | 列表实时过滤，仅含匹配标题；无匹配时 `#empty-hint` 显示「没有符合条件的题目。」 |
| **WT-PROB-006**（P1）状态徽标 | 学生对某题 AC、另一题 WA 提交过（种子题 ≥2 道即可） | 查看列表「我的状态」列 | AC 题显示 `AC` 徽标；WA/RE 等显示「尝试中」；未作答显示「未作答」 |
| **WT-PROB-007**（P1）加入班级成功 | 教师已建班，学生未入班 | `#invite-code` 填邀请码，点 `#join-btn` | `#alert`「加入班级成功」，邀请码清空，题目列表刷新出现本班题 |
| **WT-PROB-008**（P1）无效邀请码 | — | 填 `XXXXXX` | `#alert`「班级邀请码无效」（400 `INVITE_CODE_INVALID`） |
| **WT-PROB-009**（P1）重复加入班级 | 学生已入班 | 再次用同邀请码提交 | `#alert` 提示已在该班级（`ALREADY_JOINED`） |
| **WT-PROB-010**（P1）空邀请码 | — | 点加入不填 | `#alert`「请输入邀请码」 |
| **WT-PROB-011**（P1）题目跳转 | — | 点击题目标题链接 | 跳转 `/pages/problem.html?id=<id>` |

### 5.5 题目详情页与编辑器（对应 SPEC 7.4、阶段 7）

| 用例 | 前置 | 步骤 | 预期结果 |
|---|---|---|---|
| **WT-PROB-012**（P0）详情渲染 | 已入班学生 | 打开 `/pages/problem.html?id=<教师题id>` | `#problem-title`=`#id 标题`；`#problem-meta` 含难度/时限/内存；`#problem-desc`、`#sample-in`、`#sample-out` 与题目一致；`#loading` 隐藏、`#problem-content` 显示 |
| **WT-PROB-013**（P1）非法 id | — | 访问 `?id=abc` 或不存在 id | 跳转题目列表页（id 非数字）；或不存在的 id 显示错误提示（404） |
| **WT-PROB-014**（P1）未入班学生访问教师题 | 未入班学生 | 直接 URL 访问教师题详情 | `#alert` 提示题目不存在或不可见（404 `PROBLEM_NOT_FOUND`） |
| **WT-PROB-015**（P2）语言选择 | — | `#lang-select` 选 C++ / C | 可切换；编辑区提示文案显示对应编译命令（`g++ -O2 -std=c++17` / `gcc -O2 -std=c11`） |
| **WT-PROB-016**（P2）编辑器行号 | 编辑器默认占位代码 | 输入 3 行代码 | `#line-numbers` 显示 1/2/3 |
| **WT-PROB-017**（P2）Tab 缩进（无选区） | — | 光标在行首按 Tab | 插入 4 个空格；`#editor-highlight` 同步 |
| **WT-PROB-018**（P2）Tab 多行选区缩进 | 选中两行 | 按 Tab | 每行行首加 4 空格 |
| **WT-PROB-019**（P2）括号自动补全 | — | 输入 `(` | 出现 `()` 且光标在中间；输入 `{` 出现 `{}` |
| **WT-PROB-020**（P2）引号补全 / 标识符内不补全 | — | 输入 `"` | 出现 `""` 光标居中；在 `abc` 中间（光标在 b 后）输入 `"` 不补全 |
| **WT-PROB-021**（P2）右括号跳过 | — | 光标在 `()` 中间按 `)` | 不重复输入，光标右移一位 |
| **WT-PROB-022**（P2）空对内退格一次删两个 | 光标在 `()` 中间 | 按 Backspace | 直接删除 `()` 两个字符 |
| **WT-PROB-023**（P2）光标行高亮 | 输入多行代码 | 移动光标/滚动 | `#cursor-line` 跟随当前行移动 |

### 5.6 自测运行（对应 SPEC 7.4「自测运行」、API 4.3）

> 不写 submissions 表、不计入统计，是本模块核心断言点。

| 用例 | 前置 | 步骤 | 预期结果 |
|---|---|---|---|
| **WT-RUN-001**（P0）样例自测 AC | 已入班学生，打开 A+B 题 | `#code-editor` 填 AC 代码，`#lang-select`=C++，点 `#run-btn` | `#run-result` 显示「运行结果：通过（不提交、不计入提交记录）」，表格行显示 AC、耗时、内存、实际/期望输出 |
| **WT-RUN-002**（P0）自定义用例 WA | 同前 | 修改 `#run-cases` 第一条输入/期望输出，使其不匹配 | 该行显示「答案错误」徽标；summary 显示 WA |
| **WT-RUN-003**（P0）期望输出留空 | 同前 | 删除某用例期望输出后运行 | 该行显示「无期望输出」徽标；整体 summary 为「未判定（全部用例未提供期望输出）」（overall=NONE） |
| **WT-RUN-004**（P1）编译错误 | — | 输入语法错误代码（如 `int main(){ asdf }`）后运行 | `#run-result` 显示「编译错误」徽标 + 编译错误输出 |
| **WT-RUN-005**（P1）添加/删除用例 | — | 点 `#run-add-case` 增加一行；点行内「删除」移除 | 用例行数随之增减；空列表时显示「暂无自测用例」提示 |
| **WT-RUN-006**（P1）超时用例 | 打开种子题「无限循环探针」 | 填死循环代码后运行 | 用例显示「超时」（TLE） |
| **WT-RUN-007**（P1）空代码运行 | — | 清空代码点运行 | `#alert`「代码不能为空」 |
| **WT-RUN-008**（P1）自测不写库 | 记录自测前提交数 | 多次运行后查看「提交记录」页 | 提交记录列表无新增记录 |
| **WT-RUN-009**（P2）C 语言运行 | — | 选 C 语言，填 C 代码（gcc 语法） | 可正常编译运行，结果 AC |
| **WT-RUN-010**（P1）用例编辑后旧结果清除 | 已运行出 AC | 修改该用例输入 | 该行 verdict 清空，等待重新运行 |

### 5.7 提交与判题轮询（对应 SPEC 3、7.4「提交」、API 5）

> 判题异步，需轮询；断言采用 Playwright `expect` 自动等待，或手动轮询 `GET /api/submissions/:id` 至终态。

| 用例 | 前置 | 步骤 | 预期结果 |
|---|---|---|---|
| **WT-SUB-001**（P0）提交 AC | 已入班学生，A+B 题 | 填 AC 代码点 `#submit-btn` | 点击后按钮变「判题中…」；`#result-box` 显示 `排队中→编译→判题→通过` 过程（可捕捉中间状态 PENDING/COMPILING/RUNNING 至少其一，受竞速影响允许仅断言终态）；最终 `#result-status`=通过(AC)，`#result-time`/`#result-memory` 有值；出现「查看提交详情 →」链接 |
| **WT-SUB-002**（P0）提交 WA | 同前，填 `a-b` 代码 | 提交 | 终态 `#result-status`=答案错误(WA)；`#result-error` 显示首个失败测试点详情（含 Input/Expected/Actual） |
| **WT-SUB-003**（P0）编译错误 CE | 填语法错误代码 | 提交 | 终态「编译错误」，`#result-error` 显示编译器输出（截断 ≤15KB） |
| **WT-SUB-004**（P0）超时 TLE | 打开种子题「无限循环探针」（`time_limit_ms=100`） | 提交死循环代码 | 终态「超时」，错误信息含超时测试点号与 CPU 限制 |
| **WT-SUB-005**（P1）运行错误 RE | 填 `int x=1/0;` 或空指针代码 | 提交 | 终态「运行错误」，`error_message` 含退出信息 |
| **WT-SUB-006**（P1）超内存 MLE | 打开种子题「内存上限探针」（`memory_limit_mb=16`） | 填大量分配内存代码（如 `new char[300MB]`） | 终态「超内存」，错误信息含超限测试点与内存限制 |
| **WT-SUB-007**（P1）空代码提交 | — | 清空代码提交 | `#alert`「代码不能为空」，不发请求 |
| **WT-SUB-008**（P1）超长代码提交 | 输入 >100KB 文本（可脚本填充） | 提交 | `#alert`「代码过长（超过 100KB）」 |
| **WT-SUB-009**（P1）C 语言提交 | 填 C 代码 | 提交 | 判题正常，语言列显示 C |
| **WT-SUB-010**（P1）非 C/C++ 语言拒绝 | 通过接口构造 `language=python` 或页面伪造 | 提交 | 400 `PARAM_INVALID`，前端提示「不支持的编程语言」（对应 SPEC 9） |
| **WT-SUB-011**（P1）宽松比较不误判 | 提交输出含行尾空格/空行差异的正确代码 | 提交 | 仍为 AC（忽略空白与空行，对应 SPEC 3「宽松比较」） |
| **WT-SUB-012**（P1）WA 展示首失败点 | WA 提交完成后 | 点击 `#result-link` 或查看详情页 | 详情页错误区含「Wrong Answer on test case 1」及输入/期望/实际输出 |

### 5.8 提交历史与详情（对应 SPEC 7.5、7.6）

| 用例 | 前置 | 步骤 | 预期结果 |
|---|---|---|---|
| **WT-HIST-001**（P0）历史列表 | 学生已有 AC/WA 各一次 | 访问 `/pages/submissions.html` | `#submission-body` 按 ID 倒序列出本人提交；含题号、题目、语言、状态徽标、耗时、内存、时间；`#count-hint` 显示条数 |
| **WT-HIST-002**（P1）按题筛选 | 对 ≥2 道种子题均有提交 | `#problem-filter` 选某题 | 仅显示该题提交；选「全部题目」恢复 |
| **WT-HIST-003**（P1）空历史提示 | 新注册未提交学生 | 访问提交记录 | `#empty-hint`「暂无提交记录。」 |
| **WT-HIST-004**（P1）学生不可见他人提交 | 学生 A | 构造学生 B 的提交 id 直接访问详情 URL | 404 提示提交不存在或无权限（`SUBMISSION_NOT_FOUND`） |
| **WT-HIST-005**（P1）详情页渲染 | 有 AC 提交 | 打开 `/pages/submission.html?id=<sid>` | `#sub-meta` 含题目/语言/提交 ID/时间；`#timeline` 渲染状态时间线（含终态节点）；`#result-status`、耗时、内存；`#sub-code` 完整显示提交代码 |
| **WT-HIST-006**（P1）详情页错误信息 | 有 CE/WA 提交 | 打开详情 | `#result-error` 显示编译错误输出 / 首失败点详情 |
| **WT-HIST-007**（P1）非法 id | — | 访问 `?id=abc` | 跳转回提交列表页 |
| **WT-HIST-008**（P2）未登录访问 | 登出 | 直接访问提交历史页 | 重定向到登录页 |

### 5.9 个人统计（对应 SPEC 7.7）

| 用例 | 前置 | 步骤 | 预期结果 |
|---|---|---|---|
| **WT-STAT-001**（P0）统计聚合 | 学生：1 AC + 1 WA 提交 | 访问 `/pages/stats.html` | `#stat-total`=2、`#stat-ac`=1、`#stat-rate`=50%、`#stat-problems`=1 |
| **WT-STAT-002**（P1）各题状态表 | 某题 AC、另一题尝试过（种子题 ≥2 道即可） | 查看「各题目状态」 | 表含题目、提交数、AC 数、最佳状态（AC/尝试中） |
| **WT-STAT-003**（P1）提交时间分布 | 有提交 | 查看「提交时间分布」 | 按天显示提交数与 AC 数，日期为今天 |
| **WT-STAT-004**（P1）新用户空统计 | 新注册无提交 | 访问统计页 | 总数为 0，通过率 0%，表显示「暂无提交记录。」 |
| **WT-STAT-005**（P2）题目链接跳转 | 表中有题 | 点题目链接 | 跳转到对应题目详情 |

### 5.10 教师管理端（对应 SPEC 7.8「教师管理端」、阶段 8、API 6/11/12）

| 用例 | 前置 | 步骤 | 预期结果 |
|---|---|---|---|
| **WT-TEACH-001**（P0）班级创建与查看 | 教师登录，未建班 | 访问 `/pages/admin.html` → `#class-panel` 显示 → 填 `#class-name` 点 `#class-submit` | `#class-info` 显示班级名、邀请码、成员列表（空） |
| **WT-TEACH-002**（P1）重置邀请码 | 已有班级 | 点 `#invite-btn` | 邀请码变化（旧码失效：学生用旧码加入失败）；`#alert`「邀请码已重置」 |
| **WT-TEACH-003**（P1）成员列表 | 学生已入班 | 教师刷新管理页查看 `#class-info` | 成员列表包含该学生用户名 |
| **WT-TEACH-004**（P0）网页表单新建题目 | 教师 | `#prob-title`/`#prob-desc`/`#prob-sample-in`/`#prob-sample-out` 填好，`#case-add` 添加 1 个测试点，填 input/output，点 `#problem-import-btn` | `#alert`「题目已发布，ID=x」；`#problem-list` 出现新题 |
| **WT-TEACH-005**（P1）新建题目校验 | — | 空标题提交 | `#alert`「标题不能为空」；限时/内存非正数 → 对应提示 |
| **WT-TEACH-006**（P1）新建题目至少 1 测试点 | — | 不加测试点直接发布 | `#alert`「新建题目至少需要 1 个测试点」 |
| **WT-TEACH-007**（P1）编辑题目 | 教师题存在 | 点行内「修改」→ 表单回填（`startEdit` 拉详情+测试点）→ 改标题/描述 → 点 `#problem-update-btn` | `#alert`「题目已更新」；列表标题更新 |
| **WT-TEACH-008**（P1）删除题目（二次确认） | 教师题存在 | 点行内「删除」 | 弹出 `#confirm-modal`；点「确认执行」→ `#alert`「题目已删除」，列表移除；点「取消」不删除 |
| **WT-TEACH-009**（P1）教师仅能操作本人题 | 教师 A/B 各发布一题 | 教师 B 打开管理页 | B 的题目列表仅见本人题（`GET /api/problems` 可见性）；对 A 的题 ID 发起删除 → 403/报错 |
| **WT-TEACH-010**（P1）统计页 | 本班学生已提交 | 查看「统计」区 | `#st-total`、`#st-ac`、`#st-rate` 与数据一致；「各题提交统计」「学生提交情况」表格正确 |
| **WT-TEACH-011**（P0）CSV 导出 | 有提交数据 | 点 `#export-btn`（默认全部） | 触发下载 `submissions.csv`（`Content-Disposition`），文件含表头与记录（BOM+CRLF） |
| **WT-TEACH-012**（P1）CSV 按题/按人过滤 | 有数据 | 选择 `#export-problem` 某题 + `#export-user` 某学生后导出 | CSV 仅含对应题与对应学生记录 |
| **WT-TEACH-013**（P1）编辑测试点 | 教师题 | 「修改」后增删测试点行保存 | 测试点变更；删除后编号前移保持连续（可调 `GET /api/admin/problems/:id/testcases` 验证） |
| **WT-TEACH-014**（P2）教师无用户管理面板 | 教师登录 | 访问管理页 | `#user-panel` 与 `#config-panel` 不显示（仅管理员可见） |

### 5.11 管理员管理端（对应 SPEC 7.8「管理员管理端」、API 10）

| 用例 | 前置 | 步骤 | 预期结果 |
|---|---|---|---|
| **WT-ADM-001**（P0）用户列表 | 管理员登录 | 访问 `/pages/admin.html` | `#user-table` 显示全部用户（ID/用户名/角色/状态/班级/操作） |
| **WT-ADM-002**（P0）新增用户 | — | `#user-create-form` 填用户名/密码，`#user-role` 选角色，提交 | `#alert`「用户已创建」，列表出现新用户；输入框清空 |
| **WT-ADM-003**（P1）禁用/启用 | 有普通用户 | 点「禁用」 | 状态变「已禁用」（该用户登录 403 `ACCOUNT_DISABLED`）；点「启用」恢复 |
| **WT-ADM-004**（P1）删除用户 | 有普通学生 | 点「删除」→ 确认弹窗 →「确认执行」 | `#alert`「用户已删除」，列表移除；该账号无法登录 |
| **WT-ADM-005**（P1）角色调整（学生→教师） | 普通学生 | 改 `#user-role` 下拉为教师 →「保存角色」 | 角色列变为教师；该用户登录后导航含「管理」 |
| **WT-ADM-006**（P0）禁止操作自己 | 管理员当前账号 | 对自己的行点「禁用」/「删除」/降级 | `#alert`「不能对自己降级/禁用/删除」或对应错误（`CANNOT_MODIFY_SELF`） |
| **WT-ADM-007**（P0）保护内建 admin | — | 对内建 `admin` 行降级/删除 | 报错 `CANNOT_MODIFY_SELF`，不可操作 |
| **WT-ADM-008**（P0）最后一个管理员保护 | 仅剩 admin 一个管理员 | 尝试降级 admin 为 student | 报错 `LAST_ADMIN`（系统必须保留至少一个管理员） |
| **WT-ADM-009**（P1）删除有班级的教师二次确认 | 教师已建班且学生入班 | 对该教师点「删除」 | 确认弹窗显示警告「此操作会删除班级及班级成员数据，请确认」；确认后用户删除、班级级联删除（该班学生失去该教师题可见性） |
| **WT-ADM-010**（P1）降级有班级教师二次确认 | 教师已建班 | 将该教师角色改为 student 保存 | 弹窗「确认降级并删除班级」；确认后角色变 student 且班级删除 |
| **WT-ADM-011**（P0）教师邀请码读取/修改 | 管理员 | `#config-code` 显示当前码；改为新码保存 → `#alert`「教师邀请码已更新」 | `GET /api/admin/config` 返回新码；用新码注册可成教师，旧码失效（用例后恢复原码） |
| **WT-ADM-012**（P1）全局题发布 | 管理员 | 管理端发布题目（不含班级） | `created_by` 为 NULL（全局题），所有登录用户（含未入班学生）可见 |
| **WT-ADM-013**（P1）管理员改任意题/删任意题 | 存在教师题 | 对教师题「修改」「删除」 | 均可操作成功（管理员无「仅限本人」限制） |
| **WT-ADM-014**（P2）管理端统计覆盖全部 | 多角色有提交 | 管理员查看统计 | 统计范围含全局题与所有学生（教师仅本班） |
| **WT-ADM-015**（P2）管理员不创建班级 | 管理员访问管理页 | 断言页面 | 管理页**不显示** `#class-panel`（管理员不创建班级，对应 SPEC 7.8） |

### 5.12 权限与可见性（对应 SPEC 4.9、9、11）

| 用例 | 前置 | 步骤 | 预期结果 |
|---|---|---|---|
| **WT-PERM-001**（P0）学生访问管理接口 403 | 已登录学生 | 访问 `/pages/admin.html` | 被重定向回题目列表页（`admin.js` 对非 staff 跳转）；构造请求 `GET /api/admin/stats` → 403 |
| **WT-PERM-002**（P1）教师访问管理员专属接口 | 已登录教师 | 请求 `GET /api/admin/users`、`GET/PUT /api/admin/config` | 403 `FORBIDDEN`；管理页不渲染用户管理面板 |
| **WT-PERM-003**（P1）未登录访问受保护页面 | 登出/无 cookie | 访问 problems/submissions/stats/problem/admin 各页 | 均重定向 `/pages/login.html` |
| **WT-PERM-004**（P1）教师非本班学生可见性 | 学生未入班 | 学生列表/详情接口（经 UI 断言） | 教师题在列表不出现；直接访问详情 404 |
| **WT-PERM-005**（P1）教师角色加入班级 | 教师登录 | 在题目列表「加入班级」输邀请码 | 403 `FORBIDDEN`（仅 student 可入班） |
| **WT-PERM-006**（P1）会话过期 | 登录后由管理员删除该用户或等待过期（可缩短为删除会话） | 继续操作 | 接口返回 401，页面提示登录失效并被重定向 |

### 5.13 边界与异常（对应 SPEC 9）

| 用例 | 前置 | 步骤 | 预期结果 |
|---|---|---|---|
| **WT-EDGE-001**（P1）空白/空行输出宽松比对 | A+B 题含多测试点 | 提交输出带多余空行/行尾空格的 AC 逻辑 | 结果仍 AC（宽松比较忽略空白与空行） |
| **WT-EDGE-002**（P1）题目重复标题 | 已有标题 X | 教师再发布同标题题（可经接口构造） | 400，前端提示标题重复；导入回滚无脏数据（列表不出现重复） |
| **WT-EDGE-003**（P2）import 非法 JSON | — | 管理端导入接口发非法 JSON（经 UI 网络面板构造/脚本） | 400 `PARAM_INVALID` 且无脏数据 |
| **WT-EDGE-004**（P2）并发提交 | 学生已登录 | 同一页面连续快速提交 2-3 次（或双标签） | 每次提交获得独立 id，互不串号，均正确入库并判题 |
| **WT-EDGE-005**（P2）网络异常恢复 | — | 模拟离线再恢复后操作 | 前端 `fetch` 错误被捕获并展示错误信息，不白屏 |
| **WT-EDGE-006**（P1）学生越权看他人提交 | 学生 A | 直接访问学生 B 的提交详情 URL | 404 `SUBMISSION_NOT_FOUND`，页面提示无权限 |
| **WT-EDGE-007**（P2）测试点缺失 SYSTEM_ERROR | 管理员删除某题测试点文件（需服务器权限） | 提交该题 | 终态「系统错误」（`SYSTEM_ERROR`），支持教师重判后恢复（`POST /api/admin/submissions/:id/rejudge`） |

---

## 6. 用例与 SPEC 章节追踪矩阵

| 用例 | SPEC 章节 | 验收标准（第 11 章） |
|---|---|---|
| WT-AUTH-005/013、WT-PROB-011、WT-SUB-001/002/003/004、WT-HIST-001/005、WT-STAT-001 | 7.1–7.7 | ✓ 完整闭环：注册→登录→选题→提交→轮询 AC/WA/TLE/MLE/CE→历史→统计 |
| WT-SUB-011、WT-EDGE-001 | 3、11 | ✓ 宽松比较：行尾空格/空行差异不影响 AC |
| WT-TEACH-004、WT-PERM-001 | 7.8、9、11 | ✓ 教师 JSON/表单导入题目立即可见；学生访问管理接口 403 |
| WT-PROB-001/002、WT-TEACH-001/002 | 4.9、7.8、11 | ✓ 建班生成邀请码，入班可见本班题；未入班仅见全局题 |
| WT-SUB-001~006 | 3、阶段 5 | ✓ 六类判题结果（AC/WA/CE/TLE/MLE/RE） |
| WT-EDGE-007 | 3、9 | ✓ 判题进程异常置 SYSTEM_ERROR（重判接口） |

---

## 7. 执行与报告

1. **执行方式**：`pytest -q --html=report.html`（安装 `pytest-html`）；指定浏览器：`pytest --browser chromium`。
2. **基线**：建议每轮发版/部署后对 `P0` 用例集回归（约 18 条），全量 `P0+P1` 每迭代执行。
3. **定位器基线**：优先使用稳定 ID（上文已列）；动态数据用 `:has-text()` 或 `get_by_text()`；等待统一用 Playwright `expect` 自动重试，避免固定 `sleep`。
4. **失败处理**：每用例失败时截图 + 保存 HTML 快照 + 打印 `#alert` 文本，便于定位前端渲染问题。
5. **公网服务器风险提示**：共享环境数据可能被他人修改（如教师邀请码、题目），用例运行前先校验预置数据，失败后提示运行 `scripts/run_tests.sh --no-unit` 确认接口层健康。

---

## 8. 待办/建议

- [x] 判题六类结果所需特殊题（TLE/MLE 探针题）已作为种子题由 `oj_db_reset` 提供（§2.3），RE/CE 用例可直接在任一题上以对应代码触发。
- [ ] 为 `pytest-html` / Allure 集成报告输出路径（`artifacts/`）。
- [ ] 增加 `--headed` 人工演示模式，跑通第 11 章验收演示。
- [ ] 将「判题六类结果」用例所需特殊题（RE/CE 触发代码）整理为固定代码片段，降低手工依赖。
- [ ] 并发用例（WT-EDGE-004）接入 `pytest-xdist` 或独立线程模拟。
- [ ] 补充移动端/窄屏布局冒烟（管理员表单、题目详情两栏布局）。
