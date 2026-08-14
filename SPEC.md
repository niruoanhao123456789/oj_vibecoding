# SPEC.md — 仿 LeetCode 教学 OJ 系统

## 1. 项目概述

面向课程教学场景的轻量 OJ 平台。教师用 JSON 导入题目，学生注册登录后选题、在线提交 C++/C 代码，后端异步判题（子进程编译运行、CPU/内存/超时受限），学生查看结果与历史统计，教师查看统计并导出提交记录。

- **后端**：C++17 + cpp-httplib（同时托管静态前端 + 提供 REST JSON API）
- **前端**：原生 HTML + CSS + JS（无框架）
- **数据库**：MySQL（单机）
- **部署**：单机 Linux 裸跑

## 2. 角色与权限（中等权限集）

| 角色 | 能力 |
|---|---|
| 学生 | 凭邀请码加入教师班级、浏览本班可见题目、提交代码、查看判题结果/历史/个人统计 |
| 教师 | 学生能力 + 创建班级/生成邀请码、增改删题、JSON 导入题目、查看提交统计、导出 CSV |
| 管理员 | 教师能力 + 用户管理（增删改、角色调整）、管理测试用例与系统配置 |

## 3. 核心业务规则

- **题目**：初始 ≤10 题，JSON 文件导入；每题的隐藏测试点与学生可见样例分离。题目按班级隔离，可见性见 4.8。
- **班级**：一名教师一个班级，教师生成班级邀请码；学生凭邀请码加入后可见该教师发布的题目（及全局题）。
- **判题**：仅支持 C++ 与 C（g++ / gcc），不支持 Java、Python。异步判题：提交 → PENDING → 2-4 个 worker 并发判 → 结果入库 → 前端轮询。
- **判题限制**：CPU 时间上限、内存上限、总超时；超限判 TLE/MLE。
- **输出比对**：宽松比较（忽略全部空白与空行）。
- **状态机**（完整 + 容错）：`PENDING → COMPILING → COMPILE_ERROR / COMPILE_TIMEOUT / RUNNING → AC / WA / RE / TLE / MLE / SYSTEM_ERROR`；判题进程崩溃/卡死时置 `SYSTEM_ERROR` 并允许重判。

## 4. 数据模型（MySQL）

统一约定：主键均为自增 `INT UNSIGNED`；时间字段默认 `CURRENT_TIMESTAMP`；字符集 `utf8mb4`，排序规则 `utf8mb4_unicode_ci`。

### 4.1 users — 用户表

| 字段 | 类型 | 约束/默认 | 说明 |
|---|---|---|---|
| id | INT UNSIGNED | 自增主键 | 用户 ID |
| username | VARCHAR(64) | NOT NULL, UNIQUE | 登录用户名 |
| password | VARCHAR(128) | NOT NULL | 密码（教学演示，明文或简单存储均可） |
| role | ENUM('student','teacher','admin') | NOT NULL, DEFAULT 'student' | 角色 |
| status | TINYINT | NOT NULL, DEFAULT 1 | 1 正常 / 0 禁用 |
| created_at | DATETIME | DEFAULT CURRENT_TIMESTAMP | 注册时间 |

### 4.2 sessions — 会话表

| 字段 | 类型 | 约束/默认 | 说明 |
|---|---|---|---|
| token | CHAR(64) | 主键 | 随机会话 Token |
| user_id | INT UNSIGNED | NOT NULL, FK → users(id) | 所属用户 |
| created_at | DATETIME | DEFAULT CURRENT_TIMESTAMP | 创建时间 |
| expires_at | DATETIME | NOT NULL | 过期时间 |

### 4.3 problems — 题目表

| 字段 | 类型 | 约束/默认 | 说明 |
|---|---|---|---|
| id | INT UNSIGNED | 自增主键 | 题目 ID |
| title | VARCHAR(255) | NOT NULL, UNIQUE | 题目标题 |
| description | TEXT | NOT NULL | 题目描述（含输入/输出格式、数据范围） |
| sample_in | TEXT | NOT NULL | 学生可见样例输入 |
| sample_out | TEXT | NOT NULL | 学生可见样例输出 |
| time_limit_ms | INT UNSIGNED | NOT NULL, DEFAULT 1000 | 单测试点 CPU 时限（毫秒） |
| memory_limit_mb | INT UNSIGNED | NOT NULL, DEFAULT 256 | 单测试点内存上限（MB） |
| difficulty | TINYINT UNSIGNED | NOT NULL, DEFAULT 1 | 难度 1 简单 / 2 中等 / 3 困难 |
| test_dir | VARCHAR(255) | NOT NULL | 隐藏测试点目录路径（相对/绝对） |
| created_by | INT UNSIGNED | NULL, FK → users(id) | 创建人（教师/管理员） |
| created_at | DATETIME | DEFAULT CURRENT_TIMESTAMP | 创建时间 |

测试点目录 `test_dir` 约定：内含 `{编号}.in` / `{编号}.out` 成对文件（如 `1.in`、`1.out`），编号从 1 开始连续；额外支持 `score` 描述文件可选。

### 4.4 submissions — 提交表

| 字段 | 类型 | 约束/默认 | 说明 |
|---|---|---|---|
| id | INT UNSIGNED | 自增主键 | 提交 ID |
| user_id | INT UNSIGNED | NOT NULL, FK → users(id) | 提交者 |
| problem_id | INT UNSIGNED | NOT NULL, FK → problems(id) | 所属题目 |
| language | ENUM('cpp','c') | NOT NULL, DEFAULT 'cpp' | 提交语言（仅支持 C++/C） |
| code | TEXT / LONGTEXT | NOT NULL | 提交源码 |
| status | VARCHAR(20) | NOT NULL, DEFAULT 'PENDING' | 判题状态（见 3. 状态机） |
| exec_time_ms | INT UNSIGNED | NULL | 最大耗时（毫秒） |
| memory_kb | INT UNSIGNED | NULL | 峰值内存（KB） |
| error_message | TEXT | NULL | 编译错误/首个失败点详情 |
| created_at | DATETIME | DEFAULT CURRENT_TIMESTAMP | 提交时间 |

### 4.5 classes — 班级表

| 字段 | 类型 | 约束/默认 | 说明 |
|---|---|---|---|
| id | INT UNSIGNED | 自增主键 | 班级 ID |
| teacher_id | INT UNSIGNED | NOT NULL, UNIQUE, FK → users(id) | 班级教师（一名教师一个班） |
| name | VARCHAR(64) | NOT NULL, DEFAULT '默认班级' | 班级名 |
| invite_code | VARCHAR(32) | NOT NULL, UNIQUE | 班级邀请码（学生凭此加入） |
| created_at | DATETIME | DEFAULT CURRENT_TIMESTAMP | 创建时间 |

### 4.6 class_members — 班级成员表

| 字段 | 类型 | 约束/默认 | 说明 |
|---|---|---|---|
| class_id | INT UNSIGNED | NOT NULL, FK → classes(id) | 所属班级 |
| student_id | INT UNSIGNED | NOT NULL, FK → users(id) | 学生用户 |
| joined_at | DATETIME | DEFAULT CURRENT_TIMESTAMP | 加入时间 |

主键 `(class_id, student_id)`；学生可加入多个教师的班级，每个班级内唯一。

### 4.7 索引与约束

- `users.username` UNIQUE；`users.role` 普通索引。
- `sessions.token` 主键；`sessions.user_id` 索引 + 外键。
- `problems.title` UNIQUE。
- `submissions` 索引：`(user_id)`、`(problem_id)`、`(status)`、`(user_id, created_at)`；`problem_id`/`user_id` 外键。
- `classes.teacher_id` UNIQUE（一名教师一个班）；`classes.invite_code` UNIQUE。
- `class_members` 主键 `(class_id, student_id)`；`student_id` 普通索引；`class_id`/`student_id` 外键。

### 4.8 题目可见性规则

题目按班级隔离：
- **教师/管理员**：可查看全部题目。
- **学生**：仅可查看「本人加入班级的教师发布的题目」与「全局题目」（`created_by` 为 NULL，如 `oj_import` 导入的题）。未加入任何班级的学生题目列表为空。
- **未登录**：题目列表为空。

全局题对任何已入班学生可见；入班学生通过教师发布的邀请码加入班级。

### 4.9 题目 JSON 格式（导入源）

每个题目一个 JSON 文件，由 `oj_import` 导入器解析校验后写入 `problems` 表，
隐藏测试点落盘到 `data/problems/<problem_id>/`（`{编号}.in` / `{编号}.out` 成对文件）。

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| title | string | 是 | — | 题目标题，非空且 ≤255 字符，唯一 |
| description | string | 是 | — | 题目描述（含输入/输出格式、数据范围） |
| sample_in | string | 是 | — | 学生可见样例输入 |
| sample_out | string | 是 | — | 学生可见样例输出 |
| time_limit_ms | int | 否 | 1000 | 单测试点 CPU 时限（毫秒，>0） |
| memory_limit_mb | int | 否 | 256 | 单测试点内存上限（MB，>0） |
| difficulty | int | 否 | 1 | 难度：1 简单 / 2 中等 / 3 困难（取值 1-3） |
| test_dir | string | 二选一 | — | 引用外部测试点目录（相对 JSON 所在目录解析或绝对路径） |
| test_cases | array | 二选一 | — | 内联测试点数组，与 `test_dir` 互斥 |

`test_dir` 目录约定：内含 `{编号}.in` / `{编号}.out` 成对文件（编号从 1 开始，不必连续），
可选 `score` 文件（每行一个测试点分值）一并复制。

`test_cases` 元素：

| 字段 | 类型 | 必填 | 默认 | 说明 |
|---|---|---|---|---|
| input | string | 是 | — | 该测试点输入 |
| output | string | 是 | — | 该测试点期望输出 |
| name | string | 否 | — | 测试点名（可选） |
| score | int | 否 | — | 该测试点分值；任一点指定则生成 `score` 文件，缺省按 0 |

示例（test_dir 模式）：

```json
{
  "title": "A+B Problem",
  "description": "读入两个整数 a、b，输出它们的和。",
  "sample_in": "1 2\n",
  "sample_out": "3\n",
  "time_limit_ms": 1000,
  "memory_limit_mb": 256,
  "test_dir": "tests"
}
```

示例（内联 test_cases 模式）：

```json
{
  "title": "Greeting",
  "description": "读入一行姓名 name，输出 Hello, name!",
  "sample_in": "Alice\n",
  "sample_out": "Hello, Alice!\n",
  "test_cases": [
    { "input": "Alice\n", "output": "Hello, Alice!\n", "score": 50 },
    { "input": "Bob\n", "output": "Hello, Bob!\n", "score": 50 }
  ]
}
```

导入校验：必填字段缺失或类型错误、标题为空/超长、时间或内存限制非正整数、
`test_dir` 与 `test_cases` 同时给出、两者均缺、`test_cases` 为空数组、测试点缺 `input`/`output`、
标题重复、`test_dir` 目录不存在或无 `*.in` 文件、成对 `.out` 缺失 → 均报错并回滚（不留脏数据）。

## 5. API 边界（REST JSON，/api 前缀）

- 认证：`POST /api/register` `POST /api/login` `POST /api/logout` `GET /api/me`
- 题目：`GET /api/problems` `GET /api/problems/:id`（按 4.8 可见性规则过滤）
- 班级：`POST /api/class/join`（学生凭邀请码加入班级）
- 提交：`POST /api/submissions`（参数含 `language`，仅接受 `cpp`/`c`）`GET /api/submissions?user_id=` `GET /api/submissions/:id`（轮询判题状态）
- 教师/管理员：`GET/POST /api/admin/class` `POST /api/admin/class/invite` `POST /api/admin/problems/import` `PUT/DELETE /api/admin/problems/:id` `GET /api/admin/stats` `GET /api/admin/submissions/export.csv`
- 管理员：`GET/PUT /api/admin/users` `GET/PUT /api/admin/config`

## 6. 架构图

```
┌──────────────────┐  HTTP/JSON  ┌───────────────────────────────┐
│ 浏览器            │◄───────────►│ cpp-httplib 单进程             │
│ 原生 HTML/CSS/JS  │             │  ├─ 静态资源托管                │
└──────────────────┘             │  ├─ REST API + Session 鉴权    │
                                 │  ├─ MySQL 客户端（参数化查询）   │
                                 │  └─ 判题调度器（内存任务队列）    │
                                 └──────────────┬────────────────┘
                                                │ 2-4 worker 线程
                                                ▼
                                 ┌──────────────────────────────┐
                                 │ Judge Worker                 │
                                 │ g++ / gcc -O2 编译（C++/C）   │
                                 │ subprocess 运行，rlimit+超时   │
                                 │ 宽松比对隐藏测试点，回写结果    │
                                 └──────────────────────────────┘
```
 
## 6.1 项目目录结构

```
oj_vibecoding/
├── CMakeLists.txt                 # 构建配置（cpp-httplib + libmysqlclient）
├── README.md                      # 项目说明与运行指南
├── config/
│   └── server.json                # 服务配置（端口/DB/worker 数/路径）
├── sql/
│   ├── schema.sql                 # 建表脚本（6 张表 + 索引 + 初始账号）
│   └── init.sh                    # 初始化数据库辅助脚本
├── src/
│   ├── main.cpp                   # 程序入口，路由注册与启动
│   ├── log.h / log.cpp            # 统一日志模块（等级过滤/双输出/按天滚动）
│   ├── server.h / server.cpp      # HTTP 服务与静态托管
│   ├── config.h / config.cpp      # 配置加载
│   ├── db.h / db.cpp              # MySQL 访问层（参数化查询）
│   ├── problem.h / problem.cpp    # 题目 JSON 格式解析 + 导入器
│   ├── auth.h / auth.cpp          # 注册/登录/登出/Session 中间件
│   ├── ojclass.h / ojclass.cpp    # 班级管理（建班/邀请码/学生入班/可见性）
│   ├── submission.h / submission.cpp  # 提交 API + 轮询查询
│   ├── judge/
│   │   ├── compiler.h / compiler.cpp  # g++/gcc 编译模块（C++/C）
│   │   ├── runner.h / runner.cpp      # 限资源子进程运行模块
│   │   ├── compare.h / compare.cpp    # 宽松输出比对
│   │   ├── queue.h / queue.cpp        # 判题任务队列
│   │   └── worker.h / worker.cpp      # worker 池（2-4 线程）
│   └── admin/
│       ├── admin_problem.h / .cpp     # 教师题目 CRUD/导入
│       ├── admin_stats.h / .cpp       # 统计与 CSV 导出
│       └── admin_user.h / .cpp        # 管理员用户管理
├── tools/
│   └── oj_import.cpp              # 命令行题目导入工具（阶段 2 验证用）
├── frontend/
│   ├── index.html                  # 登录/注册入口页
│   ├── css/
│   │   └── style.css               # 全局样式
│   ├── js/
│   │   ├── common.js               # fetch 封装/登录态/状态徽标/轮询工具
│   │   ├── auth.js                 # 登录/注册页逻辑
│   │   ├── problems.js             # 题目列表页逻辑
│   │   ├── problem.js              # 题目详情页逻辑
│   │   ├── submissions.js          # 提交历史页逻辑
│   │   ├── submission.js           # 提交详情页逻辑
│   │   ├── stats.js                # 个人统计页逻辑
│   │   └── admin.js                # 管理端逻辑（教师/管理员）
│   └── pages/                      # 各功能页 HTML
│       ├── login.html
│       ├── register.html
│       ├── problems.html
│       ├── problem.html
│       ├── submissions.html
│       ├── submission.html
│       ├── stats.html
│       └── admin.html
├── problems/
│   ├── aplusb/
│   │   ├── problem.json            # 题目元数据（描述/样例/限制）
│   │   └── tests/                  # 隐藏测试点
│   │       ├── 1.in / 1.out
│   │       ├── 2.in / 2.out
│   │       └── ...
│   └── ...                         # 更多题目目录
├── data/
│   └── submissions/                # 判题工作区（编译产物、运行输出，按提交 ID 隔离）
├── scripts/
│   ├── import_problem.sh           # 调用导入接口/脚本导入题目
│   └── run_tests.sh                # 自动化测试入口
└── tests/
    ├── api/                        # 接口测试脚本（curl）
    ├── judge/                      # 判题六类结果用例
    └── e2e/                        # 端到端冒烟脚本
```

## 7. 前端页面

### 7.1 登录页
- 用户名 + 密码表单，提交后校验并建立 Session。
- 登录失败时显示明确错误提示（用户名不存在 / 密码错误）。
- 提供「注册」入口跳转。

### 7.2 注册页
- 用户名 + 密码 + 确认密码表单，前端做基础校验（非空、两次密码一致、用户名长度）。
- 默认注册为「学生」角色；注册成功自动跳转登录页。

### 7.3 题目列表页
- 展示可见题目：题号、标题、难度、提交数 / 通过率。
- 已登录用户展示本人每题的状态徽标（AC / 尝试中 / 未作答）。
- 支持按难度筛选与标题搜索；点击题目进入详情。
- 学生端显示「加入班级」入口：输入教师邀请码加入班级，加入后方可看到对应教师的题目与全局题目。

### 7.4 题目详情页
- 左侧：题目描述、输入/输出格式、数据范围、学生可见样例（输入/输出）。
- 右侧：代码编辑器（textarea + 简单高亮/行号）、语言固定 C++/C（支持选择 C++ 或 C）、提交按钮。
- 提交后进入判题状态展示：通过轮询实时显示 PENDING → COMPILING → RUNNING → 最终结果。
- 判题完成后展示结果状态、耗时、内存占用；WA 时展示首个失败的测试点输入/期望输出/实际输出。

### 7.5 提交历史页
- 列出本人所有提交：时间、题号、状态徽标、耗时/内存。
- 支持按题目筛选；点击进入提交详情。

### 7.6 提交详情页
- 展示代码全文、判题状态、各状态时间线、错误信息/编译输出、首失败测试点详情。

### 7.7 个人统计页
- 提交总数、AC 数、通过率；各题目状态一览；提交时间分布简表。

### 7.8 后端管理页面
- **教师管理端**：
  - 班级管理：创建班级、查看/重置班级邀请码、查看班级成员列表。
  - 题目管理：JSON 导入题目（含样例与隐藏测试点）、题目列表 CRUD（编辑、删除）。
  - 统计查看：各题提交数、AC 率、学生提交情况。
  - 提交记录导出：按题目/用户导出 CSV。
- **管理员管理端**：
  - 用户管理：用户列表、新增/禁用/删除、角色调整（学生/教师/管理员）。
  - 测试用例与系统配置管理：修改判题限制（时间/内存上限）、维护测试用例文件。

## 8. 安全

无需额外安全加固。教学演示环境，仅保留最基础的资源限制（CPU/内存/超时）与基础会话保持，不引入加密哈希、鉴权中间件、CSRF 等加固措施。

## 9. 边缘与异常

空提交/超长代码、提交 Java/Python 等非 C/C++ 代码（拒绝并返回明确错误）、编译错误消息展示、判题 worker 崩溃、测试点文件缺失、导入非法 JSON、重复题目 title、Session 过期、并发提交、无效班级邀请码、重复加入班级、学生访问教师/管理员接口（403）→ 均返回明确错误码并记录日志。

## 10. TODO 清单

### 阶段 1：工程搭建
- [x] 初始化目录结构：`src/`（后端源码）、`frontend/`（静态资源）、`sql/`（建表脚本）、`problems/`（题目 JSON 与测试点）、`scripts/`、`tests/`
- [x] 编写 `CMakeLists.txt`，引入 cpp-httplib（头文件）与 MySQL Connector/C（libmysqlclient）
- [x] 编写 `main.cpp` 启动骨架：加载配置（端口、DB 连接串、worker 数、存储路径）
- [x] 日志封装模块：提供线程安全的 `log.h`/`log.cpp` 接口，统一记录启动、HTTP 请求、判题事件与错误；支持日志等级过滤、控制台/文件双输出、按天滚动
- [x] 验证：`cmake && make` 编译通过；启动后 `GET /` 返回静态首页；DB 连接成功

### 阶段 2：数据层
- [x] 编写 `sql/schema.sql`：按第 4 章建 6 张表 + 索引 + 外键 + 初始管理员账号
- [x] 封装 DB 访问层（`db.*`）：连接管理、参数化查询辅助函数
- [x] 定义题目 JSON 格式（title/description/sample_in/sample_out/time_limit_ms/memory_limit_mb/test_cases 或 test_dir 引用），见 4.6 节
- [x] 编写 JSON 题目导入器（`problem.*`）：解析 JSON、校验字段、写 problems 表、将隐藏测试点写入 `test_dir`；命令行工具 `tools/oj_import.cpp` + `scripts/import_problem.sh`
- [x] 验证：`oj_import` 导入示例题目（`problems/aplusb` test_dir 模式、`problems/greet` 内联模式），DB 中数据与测试点文件齐全；重复标题/目录缺失回滚无脏数据

### 阶段 3：登录注册模块（认证）
**后端**
- [x] `POST /api/register`：校验用户名唯一性、长度与字符合法性，校验密码长度，写入 users 表（默认 student 角色）
- [x] `POST /api/login`：查询用户、校验密码，生成随机 token 写入 sessions，`Set-Cookie` 返回会话
- [x] `POST /api/logout`：删除会话并清除 Cookie
- [x] `GET /api/me`：凭 Cookie 返回当前用户信息（id/用户名/角色）
- [x] Session 中间件：从 Cookie 解析 token、校验未过期、注入请求上下文；无有效会话时返回 401
- [x] 错误码与提示规范：用户名已存在 / 用户名或密码错误 / 参数非法 等，返回统一 JSON 结构
- [x] 验证：注册→登录→访问受保护接口→登出 全流程 curl 通过（`tests/api/test_auth.sh` 14 项断言）

**前端**
- [x] 登录页：用户名/密码表单、错误提示展示、回车提交、登录成功跳转题目列表页
- [x] 注册页：用户名/密码/确认密码表单，前端校验（非空、两次密码一致、用户名与密码长度），注册成功跳转登录页，错误提示展示
- [x] 公共登录态：导航栏根据 `GET /api/me` 展示登录/注册入口或用户名+登出按钮
- [x] 验证：浏览器手动完成注册→登录→登出，错误场景提示正确

### 阶段 4：题目 API + 静态托管
- [x] `GET /api/problems`：列表（题号/标题/难度/提交数/通过率/本人状态）
- [x] `GET /api/problems/:id`：详情（描述/样例，不含隐藏测试点）
- [x] cpp-httplib 注册静态目录处理器，托管 `frontend/`
- [x] 验证：curl 可查列表与详情；浏览器可加载前端资源

### 阶段 5：判题引擎
- [ ] 编译模块：C++ 用 `g++ -O2 -std=c++17`、C 用 `gcc -O2 -std=c11` 编译到临时目录，捕获编译错误输出，编译超时处理
- [ ] 运行模块：`fork/exec` 子进程，`setrlimit` 设 CPU/内存/时间上限，逐测试点运行并收集输出
- [ ] 状态判定：超时→TLE、超内存→MLE、非零退出→RE；其余→宽松比对（忽略空白/空行）
- [ ] 宽松比对函数：规范化输出后逐行比较
- [ ] 任务队列：内存队列（PENDING），提交入队
- [ ] worker 池：2-4 个线程消费队列，串行编译/运行，回写 submissions 表状态与耗时/内存/错误信息
- [ ] 容错：worker 崩溃/卡死/测试点缺失→SYSTEM_ERROR，支持重判接口
- [ ] 验证：构造 AC/WA/TLE/MLE/CE/RE 六类提交，状态判定全部正确

### 阶段 6：提交流程
- [ ] `POST /api/submissions`：写 submissions(PENDING) → 入队 → 返回 submission_id
- [ ] `GET /api/submissions/:id`：返回当前状态/结果（供轮询）
- [ ] `GET /api/submissions?user_id=`：本人提交历史列表
- [ ] 验证：提交后轮询能观察到 PENDING→…→终态全过程

### 阶段 7：前端全页面
- [ ] 公共框架：导航栏（含登录态）、样式表、公共 JS（fetch 封装、状态徽标渲染、轮询工具）
- [ ] 题目列表页（筛选/搜索/状态徽标）
- [ ] 题目详情页（左侧题目区 + 右侧代码编辑器 + 提交按钮 + 轮询结果区）
- [ ] 提交历史页、提交详情页
- [ ] 个人统计页
- [ ] 验证：全页面在浏览器手动走通完整流程

### 阶段 8：管理端
- [ ] 教师班级管理：创建班级、生成/重置邀请码、查看成员（`GET/POST /api/admin/class`、`POST /api/admin/class/invite`）
- [ ] 学生加入班级：`POST /api/class/join`（凭邀请码）；题目可见性按 4.8 规则过滤
- [ ] 教师题目管理页：JSON 导入表单、题目列表（编辑/删除）
- [ ] 教师统计页 + CSV 导出（`GET /api/admin/submissions/export.csv`）
- [ ] `POST /api/admin/problems/import`、`PUT/DELETE /api/admin/problems/:id`
- [ ] 管理员用户管理页：用户列表/新增/禁用/删除/角色调整
- [ ] 管理员配置页：判题限制（时间/内存上限）、测试用例维护
- [ ] 验证：教师建班→学生凭邀请码入班→学生可见该师题目；未入班学生列表为空；学生访问管理接口返回 403

### 阶段 9：日志 + 测试
- [ ] 统一日志模块：记录启动、请求、判题事件、错误；按天滚动
- [ ] 接口测试脚本（shell/curl 或简单脚本）：覆盖注册/登录/题目/提交/判题/管理接口
- [ ] 判题单元测试：六类结果、宽松比对、资源限制边界
- [ ] 端到端冒烟测试：注册→选题→提交→判题→统计 全流程自动验证

## 11. 验收标准

- 完整闭环：注册 → 登录 → 选题 → 提交 → 轮询看到 AC/WA/TLE/MLE/CE → 历史列表 → 个人统计，全链路可演示。
- 至少 2 题含多组隐藏测试点，能正确判出 AC 与 WA；构造超时/超内存样例能判 TLE/MLE；错误语法判 CE。
- 教师可通过 JSON 导入题目并立即可见；学生无权访问管理接口（返回 403）。
- 教师创建班级并生成邀请码，学生凭邀请码加入后可见该教师题目；未入班学生题目列表为空。
- 2-4 worker 并发下 30 人规模提交无串号、无结果丢失；判题进程崩溃后提交标记 SYSTEM_ERROR。
- 宽松比较：行尾空格/空行差异不影响 AC。
