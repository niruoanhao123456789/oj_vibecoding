# OJ Vibecoding

基于 Vibecoding 实践构建的**仿 LeetCode 在线判题（OJ）教学平台**。英语文档见[README.en.md](README.en.md)。教师建班发题、导入题目，学生注册登录后在线编写 C/C++ 代码并提交，后端异步判题（实时返回 AC/WA/TLE/MLE/CE/RE 结果），并支持提交历史、个人统计、班级管理与 CSV 导出。

## 功能特性

- **在线判题**：仅支持 C++ / C，异步判题（PENDING → COMPILING → RUNNING → 终态），2-4 个 worker 并发，宽松输出比对（忽略空白与空行）。
- **判题结果全覆盖**：AC / WA / RE / TLE / MLE / CE / SYSTEM_ERROR，错误语法、死循环、超内存均能正确判定。
- **编辑器增强**：行号、语法高亮、光标行高亮、Tab 缩进、括号/引号自动补全、右符跳过、空对退格。
- **自测运行**：提交前用样例与自定义用例自测，不写提交记录、不计入统计。
- **班级管理**：教师创建班级生成邀请码，学生凭码入班后可见本班题；全局题对登录用户直接可见。
- **管理端**：教师管理班级/题目/统计/CSV 导出；管理员管理用户（增删改、禁用、角色调整）、系统配置（教师邀请码）、全局题发布与保护规则。
- **权限边界**：题目按班级隔离，学生访问管理接口 403，未登录访问受保护页面自动跳转登录页。

## 技术栈

| 层 | 技术 |
|---|---|
| 后端 | C++17 + cpp-httplib（单进程托管静态前端 + REST API + 判题 worker） |
| 前端 | 原生 HTML + CSS + JS（无框架） |
| 数据库 | MySQL（utf8mb4） |
| 判题 | `g++ -O2 -std=c++17` / `gcc -O2 -std=c11`，rlimit + 超时限制 |

## 快速开始

> 详细部署步骤（依赖安装、数据库初始化、构建、启动、systemd、测试、备份、故障排查）见 **[DEPLOY.md](DEPLOY.md)**。

```bash
# 1. 安装依赖（Ubuntu 示例，详见 DEPLOY.md）
sudo apt install -y build-essential cmake mysql-server \
  libmysqlclient-dev libcpp-httplib-dev libjsoncpp-dev

# 2. 初始化数据库（建库建账号 + 建表 + 初始管理员 admin/admin123）
sudo systemctl enable --now mysql
mysql -uroot -p < sql/init_db.sql
mysql -uroot -p < sql/schema.sql

# 3. 构建
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# 4. 初始化题目（8 道全局种子题，含 TLE/MLE 探针）
./build/oj_db_reset --config config/server.json --yes

# 5. 启动服务
./build/oj_server config/server.json

# 6. 验证
curl http://127.0.0.1:8080/api/health   # → {"status":"ok"}
# 浏览器访问 http://<服务器IP>:8080 ，用 admin / admin123 登录
```

## 项目文档

| 文档 | 内容 |
|---|---|
| [SPEC.md](SPEC.md) | 需求规格：功能点、业务规则、数据模型、API 边界、前端页面、验收标准 |
| [API.md](API.md) | 接口契约：全部 HTTP 接口、请求/响应示例、错误码对照 |
| [DEPLOY.md](DEPLOY.md) | 部署指南：环境准备、构建、数据库初始化、启动、测试、备份 |
| [WebAutoTest.md](WebAutoTest.md) | Web 自动化测试用例设计（pytest + Playwright） |

## 目录结构

```
├── CMakeLists.txt     # 构建配置
├── config/server.json # 服务配置
├── sql/               # 建库脚本与建表脚本
├── src/               # 后端源码（含 judge/ 判题引擎、admin/ 管理端）
├── tools/             # oj_import、oj_db_reset 命令行工具
├── frontend/          # 静态前端（原生 HTML/CSS/JS）
├── problems/          # 8 道种子题（JSON + 隐藏测试点）
├── data/              # 运行时数据（题目测试点、判题工作区）
├── scripts/           # 测试与导入脚本
└── tests/             # 接口 / 判题 / 单元 / 端到端测试
```

## 参与贡献

1. Fork 本仓库
2. 新建 `Feat_xxx` 分支
3. 提交代码（遵循现有代码风格与文档约定）
4. 新建 Pull Request

## 许可证

本项目采用[木兰宽松许可证，第2版](LICENSE)。
