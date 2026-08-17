# DEPLOY.en.md — OJ Vibecoding Deployment Guide

A lightweight online-judge (OJ) platform built for teaching scenarios. This guide is written against the actual codebase (`CMakeLists.txt`, `config/server.json`, `sql/*.sql`, `src/main.cpp`) and covers the complete **from-scratch single-machine Linux deployment** flow, including verification, testing, backup, and troubleshooting.

---

## 1. Overview

- **Backend**: C++17 + cpp-httplib. A single process serves the static frontend, provides the REST JSON API, and runs the built-in judge worker pool (2-4 threads).
- **Database**: MySQL (single instance).
- **Judging**: C++ / C only (`g++ -O2 -std=c++17` / `gcc -O2 -std=c11`), judged asynchronously.
- **Frontend**: Plain HTML + CSS + JS, no build step; served statically by the server.

Deployment shape: **one Linux server (with gcc/g++, MySQL, and the web service)** — start `oj_server` and the whole platform is online.

---

## 2. System Requirements

| Item | Requirement |
|---|---|
| OS | Ubuntu 20.04 / 22.04 / 24.04, or CentOS 7/8, Rocky Linux, AlmaLinux, etc. |
| Memory | ≥ 1GB (2GB recommended); scale up with judging concurrency |
| Disk | ≥ 1GB free for compilation and the judging workspace (`data/submissions/`) |
| Compiler | GCC/G++ (C++17 capable, gcc 8+ recommended). **Also required for judging** — mandatory |
| CMake | ≥ 3.16 |
| MySQL | MySQL 5.7 / 8.0 (MariaDB also works) |
| Libraries | libmysqlclient-dev, jsoncpp, cpp-httplib (header-only), pthread (bundled with glibc) |
| Port | Listens on `0.0.0.0:8080` by default |

---

## 3. Install Dependencies

### 3.1 Ubuntu / Debian

```bash
sudo apt update
sudo apt install -y build-essential cmake git \
  mysql-server \
  libmysqlclient-dev \
  libcpp-httplib-dev \
  libjsoncpp-dev
```

> Packages (on Ubuntu 24.04): `gcc/g++ 13`, `cmake 3.28`, `mysql 8.0`, `libmysqlclient-dev`, `libcpp-httplib-dev` (header-only), `libjsoncpp-dev`.

### 3.2 CentOS 8+ / Rocky Linux / AlmaLinux

```bash
sudo dnf install -y gcc gcc-c++ make cmake git \
  mysql-server \
  mysql-devel \
  jsoncpp-devel
```

- `cpp-httplib` is a **header-only** library. If there is no `cpp-httplib-devel` package in your repos, install the header manually:

  ```bash
  sudo mkdir -p /usr/local/include/httplib
  curl -sL -o /usr/local/include/httplib/httplib.h \
    https://raw.githubusercontent.com/yhirose/cpp-httplib/master/httplib.h
  ```

  > CMake locates it via `find_path(HTTPLIB_INCLUDE_DIR httplib.h)` in the system include paths; placing it under `/usr/local/include` is enough.

### 3.3 CentOS 7 (yum, requires EPEL)

```bash
sudo yum install -y epel-release
sudo yum install -y gcc gcc-c++ make cmake3 mysql-server mysql-devel jsoncpp-devel
sudo alternatives --set cmake /usr/bin/cmake3   # ensure cmake ≥ 3.16
```

---

## 4. Initialize the Database

1. **Start MySQL**:

   ```bash
   # Service name is `mysql` on Ubuntu/Debian, `mysqld` on CentOS/RHEL
   sudo systemctl enable --now mysql        # or mysqld
   sudo systemctl status mysql
   ```

2. **Create the database and account** (run `sql/init_db.sql`):

   ```bash
   mysql -uroot -p < sql/init_db.sql
   ```

   The script creates database `oj_vibecoding` (utf8mb4), user `oj@localhost` (password `oj_password`) with full privileges on that database.

3. **Create tables and the initial account** (run `sql/schema.sql`, idempotent):

   ```bash
   mysql -uroot -p < sql/schema.sql
   ```

   `schema.sql` creates 6 tables (`users`/`sessions`/`problems`/`submissions`/`classes`/`class_members`/`config`) plus indexes and foreign keys, and inserts:
   - Admin account `admin / admin123` (stored as `timestamp-salt:sha256`)
   - Teacher invite code `teacher_invite_code = TEACH-2026`

4. **Verify connectivity**:

   ```bash
   mysql -uoj -poj_password -h127.0.0.1 oj_vibecoding -e "SHOW TABLES;"
   ```

> **Credentials**: `server.json` defaults to `db_user=oj`, `db_password=oj_password`, `db_host=127.0.0.1`. If you change the DB account/password, update `config/server.json` accordingly.
> **`skip_name_resolve`**: with the default configuration, `oj@localhost` matches connections via TCP `127.0.0.1`. If MySQL has `skip_name_resolve` enabled, also create an account for `127.0.0.1`:
> ```sql
> CREATE USER IF NOT EXISTS 'oj'@'127.0.0.1' IDENTIFIED BY 'oj_password';
> GRANT ALL PRIVILEGES ON oj_vibecoding.* TO 'oj'@'127.0.0.1';
> FLUSH PRIVILEGES;
> ```

---

## 5. Build

From the project root:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Build artifacts:

| Executable | Purpose |
|---|---|
| `build/oj_server` | Main service (Web + API + judging) |
| `build/oj_import` | CLI tool to import a single problem |
| `build/oj_db_reset` | DB reset + seed test-data tool |

---

## 6. Configuration (config/server.json)

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

| Key | Description |
|---|---|
| `port` / `host` | Listen port/address (`0.0.0.0` exposes the service publicly) |
| `db_*` | MySQL connection settings, must match the initialized database |
| `worker_num` | Number of judge worker threads (2-4, per SPEC) |
| `data_dir` | Root of problem test-case data (`<data_dir>/problems/<id>/`) |
| `submission_dir` | Judging workspace (compile artifacts, run output) |
| `frontend_dir` | Frontend static asset directory |
| `log_level` | `debug` / `info` / `warn` / `error` / `off` |
| `log_dir` | Log directory (rotated daily); empty means console-only |

> **Important**: `data_dir`, `submission_dir`, `frontend_dir`, and `log_dir` are **relative paths resolved against the process working directory**. Start the service from the project root, or use absolute paths in the config.
> `data/` and `logs/` are created automatically on first run/import — no manual `mkdir` needed.

---

## 7. Initializing Problems (Seed Data)

An empty database starts with no problems. Choose one of the following:

### Option A: One-shot reset + 8 global seed problems (recommended; includes TLE/MLE probes)

```bash
./build/oj_db_reset --config config/server.json --yes
```

- Resets the database and data directory, then imports the 8 global problems under `problems/` (A+B, Greeting, Infinite Loop Probe, Max of Three, String Reverse, Memory Limit Probe, Fibonacci, Prime Check) covering difficulties 1/2/3.
- Flags: `--reset-only` (reset only, no seeding), `--seed-only` (seed only, idempotent skip of existing titles), `--admin-user/--admin-pass/--teacher-code` (override admin credentials/invite code), `--keep-admin-password` (keep admin password on reset).
- **Warning**: this wipes all business data — use it only for initialization/testing environments.

### Option B: CLI single-problem import (oj_import)

```bash
# Import a single problem JSON (test_dir or inline test_cases)
./build/oj_import problems/aplusb/problem.json --config config/server.json
# Or via the wrapper script (auto-locates build/oj_import)
./scripts/import_problem.sh problems/aplusb/problem.json
```

- Without `--created-by`, the problem is imported as a global problem (`created_by=NULL`, visible to all logged-in users). Teachers can publish class problems from the admin web page instead.

---

## 8. Starting the Service

```bash
cd /path/to/oj_vibecoding          # start from the project root so relative paths resolve
./build/oj_server config/server.json
```

Run in the background (production: use systemd, see §9):

```bash
cd /path/to/oj_vibecoding
nohup ./build/oj_server config/server.json >/dev/null 2>&1 &
```

**Verification**:

```bash
# Health endpoint
curl -s http://127.0.0.1:8080/api/health
# Expected: {"status":"ok"}

# Browser
#   http://<server-IP>:8080                          → landing page
#   http://<server-IP>:8080/pages/login.html          → login page (admin / admin123)
```

---

## 9. Service Management & Logs

### systemd service (recommended)

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

> `WorkingDirectory` must point to the project root (when paths in the config are relative).

### Logs

- Logs are rotated daily under `log_dir` (default `logs/`); verbosity is controlled by `log_level`.
- Startup, HTTP requests, and judging events/errors are all recorded here — check them first when debugging.

### Stop / Restart

```bash
systemctl stop/restart oj-server      # systemd
kill <pid>                            # direct process
```

---

## 10. Automated Tests (optional)

```bash
# All-in-one: build + unit tests (ctest) + API tests + end-to-end smoke
./scripts/run_tests.sh

# API/E2E only (service already running)
./scripts/run_tests.sh --no-unit

# Build + unit tests only
./scripts/run_tests.sh --skip-server

# Test against an externally running service
OJ_TEST_BASE=http://127.0.0.1:8080 ./scripts/run_tests.sh --no-unit
```

> If port 8080 is already in use locally, the script spins up a temporary instance on port 18082 for testing. Web automation test cases live in `WebAutoTest.md`.

---

## 11. Firewall & Public Access

- **Cloud servers**: open TCP 8080 (or your configured `port`) in the security group.
- **Linux firewall**:

  ```bash
  # firewalld (CentOS/Rocky)
  sudo firewall-cmd --permanent --add-port=8080/tcp && sudo firewall-cmd --reload
  # ufw (Ubuntu)
  sudo ufw allow 8080/tcp
  ```

- The service listens on `0.0.0.0` by default, so it is reachable publicly; change `host` to an internal IP for private-only access.

---

## 12. Backup & Restore

| Data | Description | Backup |
|---|---|---|
| Business data (users/problems/submissions/classes) | MySQL database `oj_vibecoding` | `mysqldump -uoj -p oj_vibecoding > backup.sql` |
| Problem test-case files | `data/problems/<id>/` | Archive together with the DB (aligned by problem ID) |

Restore:

```bash
mysql -uroot -p < backup.sql
```

> The judging workspace `data/submissions/` holds only temporary artifacts and can be cleared anytime; judging results are stored in the database.

---

## 13. Troubleshooting

| Symptom | Fix |
|---|---|
| `DB connection failed` at startup | Check MySQL is running; account/password/db name match `server.json`; `init_db.sql` and `schema.sql` were executed; with `skip_name_resolve` add a `'oj'@'127.0.0.1'` account |
| `mysql: command not found` | MySQL not installed or not on PATH; install per §3 |
| Port already in use | `ss -lntp | grep 8080` to find the holder; change `port` in `server.json` or free the port |
| Build error: `httplib.h` not found | Install `libcpp-httplib-dev`, or drop `httplib.h` into `/usr/local/include` manually |
| Build error: `mysqlclient` not found | Install `libmysqlclient-dev` (Ubuntu) / `mysql-devel` (CentOS) |
| Pages load but APIs 404 | `frontend_dir` correct; start from the project root |
| Submission ends in `SYSTEM_ERROR` | Check `g++`/`gcc` are installed/executable; check `data/problems/<id>/` test-case files are complete |
| Constant compile errors / compiler not found | The server must have g++/gcc installed (the judge engine depends on them) |
| Garbled Chinese text | DB and tables are utf8mb4 (set by `init_db.sql`/`schema.sql`); confirm the connection charset |

---

## 14. Directory Layout

```
oj_vibecoding/
├── CMakeLists.txt          # Build config (oj_server / oj_import / oj_db_reset / unit tests)
├── config/server.json      # Service config (port/DB/workers/paths)
├── sql/                    # init_db.sql (db + account), schema.sql (tables + initial account)
├── src/                    # Backend source (auth/problem/class/submission/judge worker/admin)
├── tools/                  # oj_import, oj_db_reset CLI tools
├── frontend/               # Static frontend (HTML/CSS/JS), no build step
├── problems/               # 8 seed problems (problem.json + test cases)
├── data/                   # Runtime data: problems/<id>/ (test cases), submissions/ (judge workspace)
├── logs/                   # Daily-rotated runtime logs
├── scripts/                # run_tests.sh, import_problem.sh
└── tests/                  # API / judge / unit / end-to-end tests
```
