# OJ Vibecoding

A **LeetCode-style online judge (OJ) teaching platform** built through Vibecoding practice. Teachers create classes, publish and import problems; students register, log in, write C/C++ code online and submit it. The backend judges asynchronously (returning AC/WA/TLE/MLE/CE/RE in real time) and supports submission history, personal statistics, class management, and CSV export.

## Features

- **Online judging**: C++ / C only, asynchronous judging (PENDING → COMPILING → RUNNING → final state) with 2-4 concurrent workers and loose output comparison (whitespace and blank lines ignored).
- **Full judging coverage**: AC / WA / RE / TLE / MLE / CE / SYSTEM_ERROR — syntax errors, infinite loops, and memory overuse are all judged correctly.
- **Editor enhancements**: line numbers, syntax highlighting, current-line highlight, Tab indentation, bracket/quote auto-completion, right-symbol skip, and paired-backspace.
- **Self-test run**: test against samples and custom cases before submitting; does not write a submission or count toward statistics.
- **Class management**: teachers create a class and generate an invite code; students join by code and then see that class's problems; global problems are directly visible to logged-in users.
- **Admin console**: teachers manage class/problem/stats/CSV export; admins manage users (create/delete/disable/role changes), system config (teacher invite code), global problem publishing, and protection rules.
- **Permission boundaries**: problems are isolated by class, students get 403 on admin endpoints, and unauthenticated users are redirected to the login page on protected pages.

## Tech Stack

| Layer | Technology |
|---|---|
| Backend | C++17 + cpp-httplib (single process: static frontend + REST API + judge workers) |
| Frontend | Plain HTML + CSS + JS (no framework) |
| Database | MySQL (utf8mb4) |
| Judging | `g++ -O2 -std=c++17` / `gcc -O2 -std=c11`, rlimit + timeout limits |

## Quick Start

> Full deployment steps (dependency install, DB init, build, run, systemd, testing, backup, troubleshooting) are in **[DEPLOY.en.md](DEPLOY.en.md)**.

```bash
# 1. Install dependencies (Ubuntu example; see DEPLOY.en.md for details)
sudo apt install -y build-essential cmake mysql-server \
  libmysqlclient-dev libcpp-httplib-dev libjsoncpp-dev

# 2. Initialize the database (create DB/account + tables + admin admin/admin123)
sudo systemctl enable --now mysql
mysql -uroot -p < sql/init_db.sql
mysql -uroot -p < sql/schema.sql

# 3. Build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j

# 4. Seed problems (8 global problems, incl. TLE/MLE probes)
./build/oj_db_reset --config config/server.json --yes

# 5. Start the server
./build/oj_server config/server.json

# 6. Verify
curl http://127.0.0.1:8080/api/health   # → {"status":"ok"}
# Open http://<server-IP>:8080 in a browser and log in with admin / admin123
```

## Documentation

| Doc | Content |
|---|---|
| [SPEC.md](SPEC.md) | Requirements: features, business rules, data model, API boundaries, pages, acceptance criteria |
| [API.md](API.md) | API contract: all HTTP endpoints, request/response examples, error-code reference |
| [DEPLOY.en.md](DEPLOY.en.md) | Deployment guide: environment prep, build, DB init, startup, testing, backup |
| [WebAutoTest.md](WebAutoTest.md) | Web automation test design (pytest + Playwright) |

## Directory Layout

```
├── CMakeLists.txt     # Build configuration
├── config/server.json # Service configuration
├── sql/               # DB-init and schema scripts
├── src/               # Backend source (incl. judge/, admin/)
├── tools/             # oj_import, oj_db_reset CLI tools
├── frontend/          # Static frontend (plain HTML/CSS/JS)
├── problems/          # 8 seed problems (JSON + hidden test cases)
├── data/              # Runtime data (test cases, judging workspace)
├── scripts/           # Test and import scripts
└── tests/             # API / judge / unit / end-to-end tests
```

## Contribution

1. Fork the repository
2. Create a `Feat_xxx` branch
3. Commit your code (follow existing code style and doc conventions)
4. Create a Pull Request

## License

This project is licensed under the [Mulan Permissive Software License, Version 2](LICENSE).
