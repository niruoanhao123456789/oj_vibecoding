# API.md — OJ 系统接口文档

本接口文档依据 [SPEC.md](SPEC.md) 与实际代码（`src/server.cpp` 中注册的路由及其 handler）总结。
仅收录**当前代码中已实现**的 HTTP 接口；SPEC 中规划但未实现的路由见文末「未实现接口」。

## 1. 通用约定

- 所有 API 均以 `/api` 前缀，返回 `application/json`。
- 统一响应结构（`/api/health` 除外）：

```json
// 成功
{ "ok": true, "data": { ... } }
// 失败
{ "ok": false, "error": { "code": "ERROR_CODE", "message": "错误说明" } }
```

- 认证方式：Cookie 会话。登录成功后服务端通过 `Set-Cookie` 下发
  `oj_session=<64位十六进制token>; Path=/; HttpOnly; Max-Age=604800`（有效期 7 天），
  后续请求携带该 Cookie。未带/失效时返回 401。
- 注册：可选 `teacher_code`，填写正确邀请码注册为教师（见 3.1）。
- 请求体一律为 JSON（`Content-Type` 不限，按 body 解析）；JSON 解析失败或字段缺失/类型错误返回 400。

## 2. 错误码对照

| HTTP 状态码 | code | 说明 |
|---|---|---|
| 400 | `PARAM_INVALID` | 参数缺失/类型错误/格式非法 |
| 400 | `INVITE_CODE_INVALID` | 班级邀请码无效 |
| 400 | `TEACHER_CODE_INVALID` | 教师注册邀请码无效 |
| 401 | `NOT_AUTHENTICATED` | 未登录或会话已过期 |
| 401 | `USER_NOT_FOUND` | 用户名不存在 |
| 401 | `WRONG_PASSWORD` | 密码错误 |
| 403 | `FORBIDDEN` | 权限不足（角色不符） |
| 403 | `ACCOUNT_DISABLED` | 账号已被禁用 |
| 403 | `CANNOT_MODIFY_SELF` | 不能对自己降级/禁用/删除，或内建 admin 不可操作 |
| 403 | `LAST_ADMIN` | 系统必须保留至少一个管理员 |
| 404 | `PROBLEM_NOT_FOUND` | 题目不存在或不可见 |
| 404 | `SUBMISSION_NOT_FOUND` | 提交不存在或无权限查看 |
| 409 | `USERNAME_EXISTS` | 用户名已存在 |
| 500 | `INTERNAL_ERROR` / 其他 | 服务端内部错误 |
| 500 | `ALREADY_JOINED` | 已在该班级中（当前实现映射为 500，语义上应为 4xx） |

## 3. 认证接口

### 3.1 `POST /api/register` — 注册

- **作用**：注册新用户。默认角色 `student`；请求体含 `teacher_code` 且与系统配置的教师邀请码一致时注册为 `teacher`。
- **请求体**：
  ```json
  { "username": "alice", "password": "secret123", "teacher_code": "TEACH-2026" }
  ```
  `teacher_code` 可选；留空或不提供则为学生。
- **参数校验**：`username` 长度 3–64，仅含字母/数字/下划线；`password` 长度 6–128。
- **响应** `200`：
  ```json
  { "ok": true, "data": { "username": "alice", "role": "teacher" } }
  ```
- **错误**：`400 PARAM_INVALID`（格式/长度非法）、`400 TEACHER_CODE_INVALID`（教师邀请码无效）、`409 USERNAME_EXISTS`（用户名已存在）、`500 INTERNAL_ERROR`。

### 3.2 `POST /api/login` — 登录

- **作用**：校验用户名密码，建立会话并下发 Cookie。
- **请求体**：
  ```json
  { "username": "alice", "password": "secret123" }
  ```
- **响应** `200`（同时 `Set-Cookie: oj_session=...`）：
  ```json
  { "ok": true, "data": { "id": 1, "username": "alice", "role": "student" } }
  ```
- **错误**：`400 PARAM_INVALID`、`401 USER_NOT_FOUND`、`401 WRONG_PASSWORD`、`403 ACCOUNT_DISABLED`、`500 INTERNAL_ERROR`。

### 3.3 `POST /api/logout` — 登出

- **作用**：删除当前会话并清除 Cookie（幂等，无有效会话也返回成功）。无需请求体。
- **响应** `200`：
  ```json
  { "ok": true, "data": {} }
  ```

### 3.4 `GET /api/me` — 当前用户信息

- **作用**：凭 Cookie 返回当前登录用户信息。
- **鉴权**：需登录。
- **响应** `200`：
  ```json
  { "ok": true, "data": { "id": 1, "username": "alice", "role": "student" } }
  ```
- **错误**：`401 NOT_AUTHENTICATED`。

## 4. 题目接口

### 4.1 `GET /api/problems` — 题目列表

- **作用**：按可见性规则（SPEC 4.8）返回题目列表，含提交数/通过率，登录用户附带本人每题状态。未登录也可访问。
- **鉴权**：可选（未登录返回空列表）。
- **响应** `200`：
  ```json
  {
    "ok": true,
    "data": {
      "problems": [
        {
          "id": 1,
          "title": "A+B Problem",
          "difficulty": 1,
          "submit_count": 5,
          "pass_rate": 80,
          "my_status": "AC"
        }
      ]
    }
  }
  ```
  - `difficulty`：1 简单 / 2 中等 / 3 困难。
  - `pass_rate`：通过率百分比整数（0–100，`AC数/提交数` 四舍五入）。
  - `my_status`：登录用户 `"AC"` / `"attempted"` / `"not_started"`；未登录为 `null`。
- **错误**：`500 INTERNAL_ERROR`。

### 4.2 `GET /api/problems/:id` — 题目详情

- **作用**：返回题目详情（不含隐藏测试点），按可见性过滤。未登录学生不可见。
- **路径参数**：`id` 题目 ID（正整数）。
- **鉴权**：可选。
- **响应** `200`：
  ```json
  {
    "ok": true,
    "data": {
      "problem": {
        "id": 1,
        "title": "A+B Problem",
        "description": "读入两个整数 a、b，输出它们的和。",
        "sample_in": "1 2\n",
        "sample_out": "3\n",
        "time_limit_ms": 1000,
        "memory_limit_mb": 256,
        "difficulty": 1
      }
    }
  }
  ```
- **错误**：`404 PROBLEM_NOT_FOUND`（不存在或不可见）。

## 5. 提交接口

### 5.1 `POST /api/submissions` — 创建提交

- **作用**：创建提交（写库状态 `PENDING`）并入判题队列，由 worker 异步判题。
- **鉴权**：需登录。
- **请求体**：
  ```json
  { "problem_id": 1, "language": "cpp", "code": "#include <cstdio>\nint main(){...}" }
  ```
- **参数约束**：`language` 仅接受 `cpp` / `c`；`code` 非空且 ≤100KB；
  `problem_id` 须为当前用户可见题目。
- **响应** `200`（返回后可轮询 `GET /api/submissions/:id`）：
  ```json
  { "ok": true, "data": { "id": 1001, "status": "PENDING" } }
  ```
- **错误**：`400 PARAM_INVALID`（语言不支持/代码为空/代码过长）、
  `404 PROBLEM_NOT_FOUND`（题目不存在或不可见）、`401 NOT_AUTHENTICATED`、`500 INTERNAL_ERROR`。

### 5.2 `GET /api/submissions` — 提交历史列表

- **作用**：返回提交历史（按 ID 倒序，上限 200 条）。学生仅本人；教师/管理员可查看全部。
- **鉴权**：需登录。
- **查询参数**：`user_id`（可选）。教师/管理员传此参数时按该用户过滤；学生忽略该参数（始终查本人）。
- **响应** `200`：
  ```json
  {
    "ok": true,
    "data": {
      "submissions": [
        {
          "id": 1001,
          "user_id": 1,
          "problem_id": 1,
          "problem_title": "A+B Problem",
          "language": "cpp",
          "status": "AC",
          "exec_time_ms": 3,
          "memory_kb": 1256,
          "created_at": "2026-08-15 12:00:00"
        }
      ]
    }
  }
  ```
  - `exec_time_ms` / `memory_kb`：判题未结束或为空时为 `null`。
- **错误**：`401 NOT_AUTHENTICATED`、`500 INTERNAL_ERROR`。

### 5.3 `GET /api/submissions/:id` — 提交详情（判题轮询）

- **作用**：返回单次提交详情，含源码与判题结果，供前端轮询判题状态。
- **路径参数**：`id` 提交 ID。
- **鉴权**：需登录；学生仅本人，教师/管理员可查任意。
- **响应** `200`：
  ```json
  {
    "ok": true,
    "data": {
      "submission": {
        "id": 1001,
        "user_id": 1,
        "problem_id": 1,
        "problem_title": "A+B Problem",
        "language": "cpp",
        "code": "#include <cstdio>\n...",
        "status": "WA",
        "exec_time_ms": 3,
        "memory_kb": 1256,
        "error_message": "Wrong Answer on test case 1\n---- Input ----\n...",
        "created_at": "2026-08-15 12:00:00"
      }
    }
  }
  ```
  - `status` 取值见第 7 节状态机；`error_message` 为编译错误/超限/首个失败点详情，无则空串。
- **错误**：`404 SUBMISSION_NOT_FOUND`（不存在或无权限）、`401 NOT_AUTHENTICATED`。

## 6. 班级接口

### 6.1 `POST /api/class/join` — 学生加入班级

- **作用**：学生凭教师邀请码加入班级，加入后可见该教师题目与全局题目。
- **鉴权**：需 `student` 角色。
- **请求体**：
  ```json
  { "invite_code": "A1B2C3D4" }
  ```
- **响应** `200`：
  ```json
  { "ok": true, "data": { "class": { "id": 1, "name": "默认班级", "invite_code": "A1B2C3D4" } } }
  ```
- **错误**：`400 INVITE_CODE_INVALID`（邀请码无效）、`403 FORBIDDEN`（非学生角色）、
  `401 NOT_AUTHENTICATED`、`500 ALREADY_JOINED`（已在该班级）。

### 6.2 `GET /api/admin/class` — 查看本人班级

- **作用**：教师/管理员查看自己创建的班级信息与成员列表。
- **鉴权**：需 `teacher` / `admin` 角色。
- **响应** `200`（未创建班级时 `class` 为 `null`）：
  ```json
  {
    "ok": true,
    "data": {
      "class": {
        "id": 1,
        "name": "默认班级",
        "invite_code": "A1B2C3D4",
        "members": [
          { "id": 2, "username": "bob" }
        ]
      }
    }
  }
  ```
- **错误**：`401 NOT_AUTHENTICATED`、`403 FORBIDDEN`、`500 INTERNAL_ERROR`。

### 6.3 `POST /api/admin/class` — 创建班级

- **作用**：教师/管理员创建班级并生成邀请码。幂等：已存在则直接返回现有班级（一名教师一个班）。
- **鉴权**：需 `teacher` / `admin` 角色。
- **请求体**（可选，缺省班级名为 `默认班级`，长度 ≤64）：
  ```json
  { "name": "计科2201" }
  ```
- **响应** `200`（创建时不含成员列表）：
  ```json
  { "ok": true, "data": { "class": { "id": 1, "name": "计科2201", "invite_code": "A1B2C3D4" } } }
  ```
- **错误**：`400 PARAM_INVALID`（班级名超长）、`401/403`、`500 INTERNAL_ERROR`。

### 6.4 `POST /api/admin/class/invite` — 重置班级邀请码

- **作用**：为本人班级重新生成邀请码（旧码失效）。无需请求体。
- **鉴权**：需 `teacher` / `admin` 角色。
- **响应** `200`：
  ```json
  { "ok": true, "data": { "class": { "id": 1, "name": "计科2201", "invite_code": "X9Y8Z7W6" } } }
  ```
- **错误**：`400 PROBLEM_NOT_FOUND`（尚未创建班级，复用该错误码）、`401/403`。

## 7. 判题接口

### 7.1 `POST /api/admin/submissions/:id/rejudge` — 重判

- **作用**：将指定提交置回 `PENDING` 并重新入队判题（可修复判题进程崩溃遗留的 `SYSTEM_ERROR`）。幂等。
- **路径参数**：`id` 提交 ID。
- **鉴权**：需 `teacher` / `admin` 角色。
- **响应** `200`：
  ```json
  { "ok": true, "data": { "id": 1001 } }
  ```
- **错误**：`404 PROBLEM_NOT_FOUND`（提交不存在，复用该错误码）、`401/403`。

## 8. 健康检查

### 8.1 `GET /api/health` — 健康检查

- **作用**：探活接口，返回服务状态（无需鉴权、无统一包裹结构）。
- **响应** `200`：
  ```json
  { "status": "ok" }
  ```

## 9. 判题状态机

提交状态 `status` 字段取值（SPEC 3.）：

```
PENDING → COMPILING → COMPILE_ERROR / COMPILE_TIMEOUT / RUNNING
        → AC / WA / RE / TLE / MLE / SYSTEM_ERROR
```

| 状态 | 含义 | error_message 内容 |
|---|---|---|
| `PENDING` | 排队待判 | — |
| `COMPILING` | 编译中 | — |
| `COMPILE_ERROR` | 编译错误 | `Compile Error` + 编译器输出（截断 15KB） |
| `COMPILE_TIMEOUT` | 编译超时 | `编译超时` |
| `RUNNING` | 运行测试点中 | — |
| `AC` | 全部测试点通过 | — |
| `WA` | 输出不匹配 | 首个失败测试点：`Wrong Answer on test case N` + Input/Expected/Actual（各截断） |
| `RE` | 运行时错误 | `Runtime Error on test case N` + 退出信息 + stderr |
| `TLE` | CPU 超时 | 超时测试点号与 CPU 限制 |
| `MLE` | 内存超限 | 超出测试点号与内存限制 |
| `SYSTEM_ERROR` | 系统错误 | 判题异常/测试点缺失/数据库错误等 |

## 10. 管理员用户管理接口

### 10.1 `GET /api/admin/users` — 用户列表

- **作用**：返回全部用户及班级信息。
- **鉴权**：需 `admin` 角色。
- **响应** `200`：
  ```json
  {
    "ok": true,
    "data": {
      "users": [
        {
          "id": 1,
          "username": "admin",
          "role": "admin",
          "status": 1,
          "created_at": "2026-08-15 12:00:00",
          "has_class": true,
          "class_name": "默认班级"
        }
      ]
    }
  }
  ```
  - `has_class`：该用户是否为某班级教师；`class_name` 对应班级名（无则 null）。
- **错误**：`401 NOT_AUTHENTICATED`、`403 FORBIDDEN`、`500 INTERNAL_ERROR`。

### 10.2 `POST /api/admin/users` — 新增用户

- **作用**：管理员创建用户（任意角色）。
- **鉴权**：需 `admin` 角色。
- **请求体**：
  ```json
  { "username": "alice", "password": "secret123", "role": "teacher" }
  ```
- **参数校验**：用户名/密码同注册规则；`role` 必须为 `student`/`teacher`/`admin`。
- **响应** `200`：返回 `data.user`（结构同列表项）。
- **错误**：`400 PARAM_INVALID`、`409 USERNAME_EXISTS`、`401/403`。

### 10.3 `PUT /api/admin/users/:id` — 修改角色/状态

- **作用**：修改用户角色或状态（禁用/启用）。
- **鉴权**：需 `admin` 角色。
- **请求体**（`role` 与 `status` 至少提供一个）：
  ```json
  { "role": "teacher", "status": 1 }
  ```
  `status`：`1` 正常 / `0` 禁用。
- **联动**：把教师降级为 `student` 时，其班级（含成员）一并删除。
- **保护规则**：禁止降级/禁用自己（`CANNOT_MODIFY_SELF`）；内建 `admin` 账号不可降级（`CANNOT_MODIFY_SELF`）；最后一个管理员不可降级（`LAST_ADMIN`）。
- **响应** `200`：返回 `data.user`（含最新 `has_class`）。
- **错误**：`400 PARAM_INVALID`、`403 CANNOT_MODIFY_SELF`/`LAST_ADMIN`、`404 USER_NOT_FOUND`。

### 10.4 `DELETE /api/admin/users/:id` — 删除用户

- **作用**：删除用户。其会话/提交/班级成员由外键级联删除；其班级随级联删除；其发布的题目 `created_by` 置 NULL（变全局题）。
- **鉴权**：需 `admin` 角色。
- **保护规则**：禁止删除自己（`CANNOT_MODIFY_SELF`）；内建 `admin` 账号不可删除（`CANNOT_MODIFY_SELF`）；最后一个管理员不可删除（`LAST_ADMIN`）。
- **响应** `200`：`{ "ok": true, "data": {} }`。
- **错误**：`403 CANNOT_MODIFY_SELF`/`LAST_ADMIN`、`404 USER_NOT_FOUND`。

### 10.5 `GET /api/admin/config` — 读取系统配置

- **作用**：返回教师注册邀请码。
- **鉴权**：需 `admin` 角色。
- **响应** `200`：
  ```json
  { "ok": true, "data": { "teacher_invite_code": "TEACH-2026" } }
  ```

### 10.6 `PUT /api/admin/config` — 修改系统配置

- **作用**：修改教师注册邀请码（注册时填此码即成为教师）。
- **鉴权**：需 `admin` 角色。
- **请求体**：
  ```json
  { "teacher_invite_code": "NEW-CODE" }
  ```
- **响应** `200`：返回更新后的配置。

## 11. 题目管理接口（教师/管理员）

### 11.1 `POST /api/admin/problems/import` — 题目 JSON 导入

- **作用**：导入题目（请求体即题目 JSON，格式见 SPEC 4.10）。管理员导入 → 全局题（`created_by` NULL，所有已入班学生可见）；教师导入 → 本班题（`created_by` 为教师 id）。
- **鉴权**：需 `teacher` / `admin` 角色。
- **请求体**：题目 JSON（含 `title`/`description`/`sample_in`/`sample_out` 及 `test_cases` 或 `test_dir`）。
- **响应** `200`：`{ "ok": true, "data": { "id": 100 } }`。
- **错误**：`400 PARAM_INVALID`（JSON 非法/字段校验失败/标题重复/目录缺失）、`401/403`。

### 11.2 `PUT /api/admin/problems/:id` — 修改题目

- **作用**：修改题目元数据；请求体含 `test_cases` 时整体替换隐藏测试点。
- **鉴权**：需 `teacher` / `admin`。教师仅能改自己发布的题；管理员可改任意题。
- **请求体**：题目 JSON（必填字段与导入一致）。
- **错误**：`400 PARAM_INVALID`（无权限/校验失败/题目不存在）、`401/403`。

### 11.3 `DELETE /api/admin/problems/:id` — 删除题目

- **作用**：删除题目及其测试点目录；其提交记录由外键级联删除。
- **鉴权**：需 `teacher` / `admin`。教师仅能删自己发布的题；管理员可删任意题。
- **响应** `200`：`{ "ok": true, "data": {} }`。
- **错误**：`400 PARAM_INVALID`（无权限/题目不存在）、`401/403`。

### 11.4 `PUT /api/admin/problems/:id/limits` — 修改判题限制

- **作用**：直接修改单题的判题限制（CPU 时限 / 内存上限），不影响测试用例。
- **鉴权**：需 `teacher` / `admin`。教师仅能改本人发布的题；管理员可改任意题。
- **请求体**（至少提供一个字段，均须为正整数）：
  ```json
  { "time_limit_ms": 2000, "memory_limit_mb": 512 }
  ```
- **响应** `200`：
  ```json
  { "ok": true, "data": { "problem": { "id": 5, "time_limit_ms": 2000, "memory_limit_mb": 512 } } }
  ```
- **错误**：`400 PARAM_INVALID`（无权限/参数非法/题目不存在）、`401/403`。

### 11.5 `GET /api/admin/problems/:id/testcases` — 列出隐藏测试点

- **作用**：返回指定题目的全部隐藏测试点（编号、输入/输出预览、分值）。
- **鉴权**：需 `teacher` / `admin`。教师仅能查看本人发布的题；管理员任意题。
- **响应** `200`：
  ```json
  {
    "ok": true,
    "data": {
      "testcases": [
        { "num": 1, "input": "1 2\n", "output": "3\n", "score": 50 }
      ]
    }
  }
  ```
  - `input`/`output` 为预览（超 4096 字节截断并附 `...(truncated)`）；`score` 无分值时 `null`。
- **错误**：`400 PARAM_INVALID`（无权限/题目不存在）、`401/403`。

### 11.6 `POST /api/admin/problems/:id/testcases` — 追加测试点

- **作用**：为题目追加一个隐藏测试点（写入 `<下一个编号>.in` / `.out`），同步维护 `score` 文件。
- **鉴权**：需 `teacher` / `admin`。教师仅能改本人发布的题；管理员任意题。
- **请求体**：
  ```json
  { "input": "1 2\n", "output": "3\n", "score": 50 }
  ```
  `input`/`output` 必填字符串（各 ≤4MB），`score` 可选整数。
- **响应** `200`：
  ```json
  { "ok": true, "data": { "testcase": { "num": 4, "input": "1 2\n", "output": "3\n", "score": 50 } } }
  ```
- **错误**：`400 PARAM_INVALID`、`401/403`。

### 11.7 `DELETE /api/admin/problems/:id/testcases/:num` — 删除测试点

- **作用**：删除编号为 `num` 的测试点，后续编号前移保持连续；同步更新 `score` 文件。
- **路径参数**：`num` 测试点编号（正整数）。
- **鉴权**：需 `teacher` / `admin`。教师仅能改本人发布的题；管理员任意题。
- **响应** `200`：`{ "ok": true, "data": {} }`。
- **错误**：`400 PARAM_INVALID`（无权限/测试点不存在）、`401/403`。

## 12. 统计与导出（教师/管理员）

### 12.1 `GET /api/admin/stats` — 统计

- **作用**：返回各题提交数/AC 率与各学生提交情况。
  - 教师：仅统计自己发布的题目（本班题），及本班学生的提交情况（只统计本班题目的提交）；
  - 管理员：统计全部题目与全部有提交记录的学生。
- **鉴权**：需 `teacher` / `admin` 角色。
- **响应** `200`：
  ```json
  {
    "ok": true,
    "data": {
      "total_submit": 42,
      "total_ac": 10,
      "total_rate": 24,
      "problem_stats": [
        { "problem_id": 1, "title": "A+B Problem", "submit_count": 20, "ac_count": 5, "pass_rate": 25 }
      ],
      "student_stats": [
        { "user_id": 2, "username": "bob", "submit_count": 8, "ac_count": 2, "pass_rate": 25 }
      ]
    }
  }
  ```
- **错误**：`401 NOT_AUTHENTICATED`、`403 FORBIDDEN`、`500 INTERNAL_ERROR`。

### 12.2 `GET /api/admin/submissions/export.csv` — 提交记录 CSV 导出

- **作用**：导出提交记录为 CSV（UTF-8 带 BOM、CRLF 行尾，Excel 可直接打开）。
  教师仅能导出自己发布题目的提交；管理员可导出全部。
- **鉴权**：需 `teacher` / `admin` 角色。
- **查询参数**（均可选）：`problem_id` 按题目过滤；`user_id` 按用户过滤。
- **响应** `200`：`text/csv`，带 `Content-Disposition: attachment`。列：
  `id,username,problem_id,problem_title,language,status,exec_time_ms,memory_kb,created_at`
- **错误**：`401 NOT_AUTHENTICATED`、`403 FORBIDDEN`、`500 INTERNAL_ERROR`。

## 13. 未实现接口（SPEC 规划中）

以下路由在 `SPEC.md` 第 5 节中规划，但当前代码（`src/server.cpp`）尚未注册，**不可调用**：

- （阶段 8 全部接口已实现，当前无未实现路由）

前端已实现的页面可参考 `frontend/pages/` 下的 HTML 与 `frontend/js/` 中的调用方式。
