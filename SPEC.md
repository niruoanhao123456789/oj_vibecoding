# SPEC.md — 仿 LeetCode 教学 OJ 系统

## 1. 项目概述

面向课程教学场景的轻量 OJ 平台。教师用 JSON 导入题目，学生注册登录后选题、在线提交 C++ 代码，后端异步判题（子进程编译运行、CPU/内存/超时受限），学生查看结果与历史统计，教师查看统计并导出提交记录。

- **后端**：C++17 + cpp-httplib（同时托管静态前端 + 提供 REST JSON API）
- **前端**：原生 HTML + CSS + JS（无框架）
- **数据库**：MySQL（单机）
- **部署**：单机 Linux 裸跑

## 2. 角色与权限（中等权限集）

| 角色 | 能力 |
|---|---|
| 学生 | 浏览题目、提交代码、查看判题结果/历史/个人统计 |
| 教师 | 学生能力 + 增改删题、JSON 导入题目、查看提交统计、导出 CSV |
| 管理员 | 教师能力 + 用户管理（增删改、角色调整）、管理测试用例与系统配置 |

## 3. 核心业务规则

- **题目**：初始 ≤10 题，JSON 文件导入；每题的隐藏测试点与学生可见样例分离。
- **判题**：仅 C++（g++）。异步判题：提交 → PENDING → 2-4 个 worker 并发判 → 结果入库 → 前端轮询。
- **判题限制**：CPU 时间上限、内存上限、总超时；超限判 TLE/MLE。
- **输出比对**：宽松比较（忽略全部空白与空行）。
- **状态机**（完整 + 容错）：`PENDING → COMPILING → COMPILE_ERROR / COMPILE_TIMEOUT / RUNNING → AC / WA / RE / TLE / MLE / SYSTEM_ERROR`；判题进程崩溃/卡死时置 `SYSTEM_ERROR` 并允许重判。

## 4. 数据模型（MySQL）

- `users(id, username, password_hash, role, created_at)`
- `sessions(token, user_id, expires_at)`
- `problems(id, title, description, sample_in, sample_out, time_limit_ms, memory_limit_mb, test_dir, created_by)`
- `submissions(id, user_id, problem_id, code, status, exec_time_ms, memory_kb, error_message, created_at)`

## 5. API 边界（REST JSON，/api 前缀）

- 认证：`POST /api/register` `POST /api/login` `POST /api/logout` `GET /api/me`
- 题目：`GET /api/problems` `GET /api/problems/:id`
- 提交：`POST /api/submissions` `GET /api/submissions?user_id=` `GET /api/submissions/:id`（轮询判题状态）
- 教师：`POST /api/admin/problems/import` `PUT/DELETE /api/admin/problems/:id` `GET /api/admin/stats` `GET /api/admin/submissions/export.csv`
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
                                 │ g++ -O2 -std=c++17 编译      │
                                 │ subprocess 运行，rlimit+超时   │
                                 │ 宽松比对隐藏测试点，回写结果    │
                                 └──────────────────────────────┘
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
- 展示全部题目：题号、标题、难度、提交数 / 通过率。
- 已登录用户展示本人每题的状态徽标（AC / 尝试中 / 未作答）。
- 支持按难度筛选与标题搜索；点击题目进入详情。

### 7.4 题目详情页
- 左侧：题目描述、输入/输出格式、数据范围、学生可见样例（输入/输出）。
- 右侧：代码编辑器（textarea + 简单高亮/行号）、语言固定 C++、提交按钮。
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
  - 题目管理：JSON 导入题目（含样例与隐藏测试点）、题目列表 CRUD（编辑、删除）。
  - 统计查看：各题提交数、AC 率、学生提交情况。
  - 提交记录导出：按题目/用户导出 CSV。
- **管理员管理端**：
  - 用户管理：用户列表、新增/禁用/删除、角色调整（学生/教师/管理员）。
  - 测试用例与系统配置管理：修改判题限制（时间/内存上限）、维护测试用例文件。

## 8. 安全

无需额外安全加固。教学演示环境，仅保留最基础的资源限制（CPU/内存/超时）与基础会话保持，不引入加密哈希、鉴权中间件、CSRF 等加固措施。

## 9. 边缘与异常

空提交/超长代码、编译错误消息展示、判题 worker 崩溃、测试点文件缺失、导入非法 JSON、重复题目 title、Session 过期、并发提交 → 均返回明确错误码并记录日志。

## 10. TODO 清单

1. **工程搭建**：CMake、cpp-httplib 与 MySQL client 集成、目录结构
2. **数据层**：建表 SQL、JSON 题目导入器（含测试点目录落地）
3. **认证**：注册/登录/登出/Session/角色
4. **题目 API + 静态托管**
5. **判题引擎**：编译、限资源子进程运行、宽松比对、2-4 worker 池、任务队列、容错重判
6. **提交流程**：提交 API + 前端轮询
7. **前端全页面**
8. **管理端**：教师导入/CRUD/统计/CSV，管理员用户管理
9. **日志 + 测试**

## 11. 验收标准

- 完整闭环：注册 → 登录 → 选题 → 提交 → 轮询看到 AC/WA/TLE/MLE/CE → 历史列表 → 个人统计，全链路可演示。
- 至少 2 题含多组隐藏测试点，能正确判出 AC 与 WA；构造超时/超内存样例能判 TLE/MLE；错误语法判 CE。
- 教师可通过 JSON 导入题目并立即可见；学生无权访问管理接口（返回 403）。
- 2-4 worker 并发下 30 人规模提交无串号、无结果丢失；判题进程崩溃后提交标记 SYSTEM_ERROR。
- 宽松比较：行尾空格/空行差异不影响 AC。
