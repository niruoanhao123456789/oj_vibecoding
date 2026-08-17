# DEPLOY.md — OJ Vibecoding 部署指南

面向课程教学场景的轻量 OJ 平台。本文档基于实际代码（`CMakeLists.txt`、`config/server.json`、`sql/*.sql`、`src/main.cpp`）编写，覆盖**单机 Linux 从零部署到启动服务**的完整流程，并附带验证、测试、备份与常见问题。

---

## 1. 概述

- **后端**：C++17 + cpp-httplib，单进程同时托管静态前端 + 提供 REST JSON API + 内置判题 worker 池（2-4 线程）。
- **数据库**：MySQL（单机）。
- **判题**：仅支持 C++ / C（`g++ -O2 -std=c++17` / `gcc -O2 -std=c11`），异步判题。
- **前端**：原生 HTML + CSS + JS，无需额外构建，由服务静态托管。

部署形态：**一台 Linux 服务器（含 gcc/g++、MySQL、Web 服务），启动 `oj_server` 即可对外提供全部功能**。

---

## 2. 系统要求

| 项 | 要求 |
|---|---|
| 操作系统 | Ubuntu 20.04 / 22.04 / 24.04，或 CentOS 7/8、Rocky Linux、AlmaLinux 等 |
| 内存 | ≥1GB（建议 2GB），判题并发多时按需加大 |
| 磁盘 | 预留 ≥1GB 用于编译与判题工作区（`data/submissions/`） |
| 编译器 | GCC/G++（支持 C++17，建议 gcc 8+），同时**用于判题**，必须安装 |
| CMake | ≥ 3.16 |
| MySQL | MySQL 5.7 / 8.0（MariaDB 亦可） |
| 库依赖 | libmysqlclient-dev、jsoncpp、cpp-httplib（仅头文件）、pthread（系统自带） |
| 端口 | 默认监听 `0.0.0.0:8080` |

---

## 3. 安装依赖

### 3.1 Ubuntu / Debian

```bash
sudo apt update
sudo apt install -y build-essential cmake git \
  mysql-server \
  libmysqlclient-dev \
  libcpp-httplib-dev \
  libjsoncpp-dev
```

> 对应包（以 Ubuntu 24.04 为例）：`gcc/g++ 13`、`cmake 3.28`、`mysql 8.0`、`libmysqlclient-dev`、`libcpp-httplib-dev`（header-only）、`libjsoncpp-dev`。

### 3.2 CentOS 8+ / Rocky Linux / AlmaLinux

```bash
sudo dnf install -y gcc gcc-c++ make cmake git \
  mysql-server \
  mysql-devel \
  jsoncpp-devel
```

- `cpp-httplib` 为**仅头文件**库，若仓库无 `cpp-httplib-devel` 包，可手动安装头文件：

  ```bash
  sudo mkdir -p /usr/local/include/httplib
  curl -sL -o /usr/local/include/httplib/httplib.h \
    https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h
  ```

  > CMake 通过 `find_path(HTTPLIB_INCLUDE_DIR httplib.h)` 在系统 include 路径中查找，放入 `/usr/local/include` 即可被找到。

### 3.3 CentOS 7（yum，需启用 EPEL）

```bash
sudo yum install -y epel-release
sudo yum install -y gcc gcc-c++ make cmake3 mysql-server mysql-devel jsoncpp-devel
sudo alternatives --set cmake /usr/bin/cmake3   # cmake 版本 ≥3.16
```

---

## 4. 初始化数据库

1. **启动 MySQL 服务**：

   ```bash
   # Ubuntu/Debian 服务名为 mysql；CentOS/RHEL 为 mysqld
   sudo systemctl enable --now mysql        # 或 mysqld
   sudo systemctl status mysql
   ```

2. **创建数据库与账号**（执行 `sql/init_db.sql`）：

   ```bash
   mysql -uroot -p < sql/init_db.sql
   ```

   脚本内容：创建数据库 `oj_vibecoding`（utf8mb4）、创建账号 `oj@localhost`（密码 `oj_password`）并授权。

3. **建表 + 初始账号**（执行 `sql/schema.sql`，幂等可重复执行）：

   ```bash
   mysql -uroot -p < sql/schema.sql
   ```

   `schema.sql` 会创建 6 张表（`users`/`sessions`/`problems`/`submissions`/`classes`/`class_members`/`config`）及索引、外键，并写入：
   - 管理员账号 `admin / admin123`（密码以「时间戳盐:sha256」存储）
   - 教师注册邀请码 `teacher_invite_code = TEACH-2026`

4. **校验连接**：

   ```bash
   mysql -uoj -poj_password -h127.0.0.1 oj_vibecoding -e "SHOW TABLES;"
   ```

> **账号说明**：`server.json` 默认使用 `db_user=oj`、`db_password=oj_password`、`db_host=127.0.0.1`。若修改数据库账号/密码，请同步修改 `config/server.json`。
> **`skip_name_resolve`**：默认配置下 `oj@localhost` 通过 `127.0.0.1` TCP 连接可匹配；若 MySQL 开启了 `skip_name_resolve`，需额外创建 `'oj'@'127.0.0.1'` 账号：
> ```sql
> CREATE USER IF NOT EXISTS 'oj'@'127.0.0.1' IDENTIFIED BY 'oj_password';
> GRANT ALL PRIVILEGES ON oj_vibecoding.* TO 'oj'@'127.0.0.1';
> FLUSH PRIVILEGES;
> ```

---

## 5. 构建

在项目根目录执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

构建产物：

| 可执行文件 | 用途 |
|---|---|
| `build/oj_server` | 主服务（Web + API + 判题） |
| `build/oj_import` | 命令行单题导入工具 |
| `build/oj_db_reset` | 数据库重置 + 预置测试数据工具 |

---

## 6. 配置（config/server.json）

```json
{
  "port": 8080,
  "host": "0.0.0.0",
  "db_host": "127.0.0.1",
  "db_port": 3306,
  "db_user": "oj",
  "db_password": "oj_password",
  "db_name": "oj_vibecoding",
  "worker_num": 2,
  "data_dir": "data",
  "submission_dir": "data/submissions",
  "frontend_dir": "frontend",
  "log_level": "info",
  "log_dir": "logs"
}
```

| 配置项 | 说明 |
|---|---|
| `port` / `host` | 监听端口与地址（`0.0.0.0` 表示对外可访问） |
| `db_*` | MySQL 连接信息，需与数据库初始化一致 |
| `worker_num` | 判题 worker 线程数（2-4，SPEC 约定范围） |
| `data_dir` | 题目测试点数据根目录（`<data_dir>/problems/<id>/`） |
| `submission_dir` | 判题工作区（编译产物、运行输出） |
| `frontend_dir` | 前端静态资源目录 |
| `log_level` | `debug` / `info` / `warn` / `error` / `off` |
| `log_dir` | 日志目录（按天滚动），为空则仅控制台输出 |

> **重要**：`data_dir`、`submission_dir`、`frontend_dir`、`log_dir` 均为**相对路径，基于进程工作目录解析**。请从项目根目录启动服务，或在配置中改为绝对路径。
> `data/`、`logs/` 目录会在首次运行/导入时自动创建，无需手动 mkdir。

---

## 7. 初始化题目（预置数据）

空数据库启动后没有任何题目。二选一：

### 方式 A：一键重置 + 8 道全局种子题（推荐，含 TLE/MLE 探针题）

```bash
./build/oj_db_reset --config config/server.json --yes
```

- 重置数据库与数据目录，并导入 `problems/` 下 8 道全局题（A+B、Greeting、无限循环探针、三数最大值、字符串反转、内存上限探针、斐波那契数列、素数判断），覆盖难度 1/2/3。
- 参数：`--reset-only`（仅重置不造数）、`--seed-only`（仅补种子题，幂等跳过已存在）、`--admin-user/--admin-pass/--teacher-code`（覆盖管理员与邀请码）、`--keep-admin-password`（重置时保留 admin 密码）。
- **注意**：会清空全部业务数据，仅限初始化/测试环境使用。

### 方式 B：命令行单题导入（oj_import）

```bash
# 导入单个题目 JSON（test_dir 或内联 test_cases 均可）
./build/oj_import problems/aplusb/problem.json --config config/server.json
# 或使用脚本（自动定位 build/oj_import）
./scripts/import_problem.sh problems/aplusb/problem.json
```

- 不带 `--created-by` 时导入为全局题（`created_by=NULL`，所有登录用户可见）；教师可通过管理端网页发布本班题。

---

## 8. 启动服务

```bash
cd /path/to/oj_vibecoding          # 从项目根目录启动，保证相对路径正确
./build/oj_server config/server.json
```

后台运行（生产建议用 systemd，见 §9）：

```bash
cd /path/to/oj_vibecoding
nohup ./build/oj_server config/server.json >/dev/null 2>&1 &
```

**验证**：

```bash
# 探活接口
curl -s http://127.0.0.1:8080/api/health
# 期望输出: {"status":"ok"}

# 浏览器访问
#   http://<服务器IP>:8080          → 落地首页
#   http://<服务器IP>:8080/pages/login.html   → 登录页（admin / admin123）
```

---

## 9. 服务管理与日志

### systemd 服务（推荐）

```ini
# /etc/systemd/system/oj-server.service
[Unit]
Description=OJ Vibecoding server
After=network.target mysql.service

[Service]
Type=simple
WorkingDirectory=/opt/oj_vibecoding
ExecStart=/opt/oj_vibecoding/build/oj_server /opt/oj_vibecoding/config/server.json
Restart=on-failure
RestartSec=3

[Install]
WantedBy=multi-user.target
```

```bash
sudo systemctl daemon-reload
sudo systemctl enable --now oj-server
sudo systemctl status oj-server
```

> `WorkingDirectory` 必须指向项目根目录（配置为相对路径时）。

### 日志

- 日志按天滚动写入 `log_dir`（默认 `logs/`），等级由 `log_level` 控制。
- 判题相关错误、HTTP 请求、启动信息均记录于此，排查问题优先查看。

### 停止 / 重启

```bash
systemctl stop/restart oj-server      # systemd 方式
kill <pid>                            # 直接进程方式
```

---

## 10. 自动化测试（可选）

```bash
# 一键：构建 + 单元测试（ctest）+ 接口测试 + 端到端冒烟
./scripts/run_tests.sh

# 仅接口/端到端（服务已启动）
./scripts/run_tests.sh --no-unit

# 仅构建 + 单元测试
./scripts/run_tests.sh --skip-server

# 指定外部运行中的服务
OJ_TEST_BASE=http://127.0.0.1:8080 ./scripts/run_tests.sh --no-unit
```

> 若本机 8080 已被占用，脚本会在 18082 端口临时启动实例测试。Web 自动化用例见 `WebAutoTest.md`。

---

## 11. 防火墙与公网访问

- **云服务器**：在安全组放行 TCP 8080（或你配置的 `port`）。
- **Linux 防火墙**：

  ```bash
  # firewalld（CentOS/Rocky）
  sudo firewall-cmd --permanent --add-port=8080/tcp && sudo firewall-cmd --reload
  # ufw（Ubuntu）
  sudo ufw allow 8080/tcp
  ```

- 服务默认监听 `0.0.0.0`，公网可直接访问；如需仅内网，可将 `host` 改为内网 IP。

---

## 12. 备份与恢复

| 数据 | 说明 | 备份方式 |
|---|---|---|
| 业务数据（用户/题目/提交/班级） | MySQL 库 `oj_vibecoding` | `mysqldump -uoj -p oj_vibecoding > backup.sql` |
| 题目测试点文件 | `data/problems/<id>/` | 随数据库按题目 ID 对齐，可一并归档 |

恢复：

```bash
mysql -uroot -p < backup.sql
```

> 判题工作区 `data/submissions/` 为临时产物，可随时清空；提交结果以数据库为准。

---

## 13. 常见问题排查

| 现象 | 处理 |
|---|---|
| 启动报 `DB connection failed` | 检查 MySQL 是否启动；账号/密码/库名是否与 `server.json` 一致；是否已执行 `init_db.sql` 与 `schema.sql`；`skip_name_resolve` 时补 `'oj'@'127.0.0.1'` 账号 |
| `mysql: command not found` | MySQL 未安装或不在 PATH，按 §3 安装 |
| 端口被占用 | `ss -lntp | grep 8080` 查看占用，修改 `server.json` 的 `port` 或释放端口 |
| 编译报错找不到 `httplib.h` | 安装 `libcpp-httplib-dev`，或手动将 `httplib.h` 放入 `/usr/local/include` |
| 编译报错找不到 `mysqlclient` | 安装 `libmysqlclient-dev`（Ubuntu）/ `mysql-devel`（CentOS） |
| 页面可访问但 API 返回 404 | `frontend_dir` 指向正确、启动目录为项目根目录 |
| 提交后终态 `SYSTEM_ERROR` | 检查 `g++`/`gcc` 是否安装且可执行；检查该题 `data/problems/<id>/` 测试点文件是否完整 |
| 判题结果恒为编译错误/找不到编译器 | 服务器需装有 g++/gcc（判题引擎依赖） |
| 中文乱码 | 数据库、表均使用 utf8mb4（`init_db.sql`/`schema.sql` 已配置），确认连接字符集 |

---

## 14. 目录结构速览

```
oj_vibecoding/
├── CMakeLists.txt          # 构建配置（oj_server / oj_import / oj_db_reset / 单元测试）
├── config/server.json      # 服务配置（端口/DB/worker/路径）
├── sql/                    # init_db.sql（库与账号）、schema.sql（建表+初始账号）
├── src/                    # 后端源码（认证/题目/班级/提交/判题 worker/管理端）
├── tools/                  # oj_import、oj_db_reset 命令行工具
├── frontend/               # 静态前端（HTML/CSS/JS），无需构建
├── problems/               # 8 道种子题（problem.json + 测试点）
├── data/                   # 运行时数据：problems/<id>/（测试点）、submissions/（判题工作区）
├── logs/                   # 运行日志（按天滚动）
├── scripts/                # run_tests.sh、import_problem.sh
└── tests/                  # 接口/判题/单元/端到端测试
```
