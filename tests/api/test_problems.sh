#!/usr/bin/env bash
# tests/api/test_problems.sh — 阶段 4 题目 API 测试（curl）。
#
# 覆盖：
#   GET /api/problems  列表（字段完整：id/title/difficulty/submit_count/pass_rate/my_status）
#   GET /api/problems/:id 详情（含样例、不含隐藏测试点目录）
#   不存在的题目 id -> 404 + PROBLEM_NOT_FOUND
#   非数字 id -> 404
#   未登录 my_status 为 null；登录后为 not_started
#   静态资源可加载
#
# 用法：
#   ./tests/api/test_problems.sh [base_url]
#   默认 base_url=http://127.0.0.1:8080
set -uo pipefail

BASE="${1:-http://127.0.0.1:8080}"
PASS=0
FAIL=0

check() {
    # check <描述> <期望子串> <实际输出>
    local desc="$1" want="$2" got="$3"
    if [[ "$got" == *"$want"* ]]; then
        echo "PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $desc (期望含 [$want]，实际: $got)"
        FAIL=$((FAIL + 1))
    fi
}

check_code() {
    # check_code <描述> <期望HTTP码> <实际输出含码>
    local desc="$1" want="$2" got="$3"
    if [[ "$got" == *"|$want"* ]]; then
        echo "PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $desc (期望 HTTP $want，实际: $got)"
        FAIL=$((FAIL + 1))
    fi
}

JAR="$(mktemp)"
UNIQ="stu_$(date +%s%N)"

echo "== 准备（管理员登录，admin 可见全部题目） =="
R=$(curl -s -c "$JAR" -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d '{"username":"admin","password":"admin123"}')
check "管理员登录成功" '"ok":true' "$R"

echo "== 列表 GET /api/problems =="
R=$(curl -s -b "$JAR" "$BASE/api/problems")
check "返回 ok:true" '"ok":true' "$R"
check "含 A+B Problem" 'A+B Problem' "$R"
check "含 Greeting" 'Greeting' "$R"
check "含 difficulty 字段" '"difficulty"' "$R"
check "含 submit_count 字段" '"submit_count"' "$R"
check "含 pass_rate 字段" '"pass_rate"' "$R"
check "登录后 my_status 存在" '"my_status"' "$R"

echo "== 详情 GET /api/problems/:id =="
# 取列表中第一个题目的 id
PID=$(curl -s -b "$JAR" "$BASE/api/problems" | grep -o '"id":[0-9]*' | head -1 | cut -d: -f2)
if [[ -z "$PID" ]]; then
    echo "FAIL: 无法从列表解析题目 id"
    FAIL=$((FAIL + 1))
else
    R=$(curl -s -b "$JAR" "$BASE/api/problems/$PID")
    check "详情返回 ok:true" '"ok":true' "$R"
    check "含 description 字段" '"description"' "$R"
    check "含 sample_in 字段" '"sample_in"' "$R"
    check "含 sample_out 字段" '"sample_out"' "$R"
    check "含 time_limit_ms 字段" '"time_limit_ms"' "$R"
    check "含 memory_limit_mb 字段" '"memory_limit_mb"' "$R"
    if [[ "$R" != *'"test_dir"'* ]]; then
        echo "PASS: 详情不泄露 test_dir"
        PASS=$((PASS + 1))
    else
        echo "FAIL: 详情泄露 test_dir"
        FAIL=$((FAIL + 1))
    fi
fi

echo "== 异常场景 =="
R=$(curl -s -w "|%{http_code}" "$BASE/api/problems/999999")
check_code "不存在的题目 HTTP 404" 404 "$R"
check "错误码 PROBLEM_NOT_FOUND" 'PROBLEM_NOT_FOUND' "$R"

R=$(curl -s -w "|%{http_code}" "$BASE/api/problems/abc")
check_code "非数字 id HTTP 404" 404 "$R"

echo "== 未登录/未入班可见性 =="
R=$(curl -s "$BASE/api/problems")
check "未登录列表为空" '"problems":[]' "$R"

R=$(curl -s -X POST "$BASE/api/register" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$UNIQ\",\"password\":\"pass123\"}")
curl -s -c "$JAR" -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$UNIQ\",\"password\":\"pass123\"}" > /dev/null
# 未入班学生按可见性规则（SPEC 4.8）列表为空
R=$(curl -s -b "$JAR" "$BASE/api/problems")
check "未入班学生列表为空" '"problems":[]' "$R"
curl -s -b "$JAR" -X POST "$BASE/api/logout" > /dev/null

echo "== 静态资源 =="
R=$(curl -s -w "|%{http_code}" -o /dev/null "$BASE/")
check_code "GET / HTTP 200" 200 "$R"
R=$(curl -s -w "|%{http_code}" -o /dev/null "$BASE/pages/problems.html")
check_code "GET /pages/problems.html HTTP 200" 200 "$R"

rm -f "$JAR"

echo "----"
echo "通过 $PASS，失败 $FAIL"
[[ $FAIL -eq 0 ]]
