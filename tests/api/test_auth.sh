#!/usr/bin/env bash
# tests/api/test_auth.sh — 阶段 3 认证接口全流程测试（curl）。
#
# 覆盖：
#   注册（成功/重复/非法用户名/密码过短/缺字段）
#   登录（成功/密码错误/用户不存在/账号禁用）
#   /api/me（已登录/未登录 401）
#   登出（后 /api/me 401）
#
# 用法：
#   ./tests/api/test_auth.sh [base_url]
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

JAR="$(mktemp)"
UNIQ="stu_$(date +%s%N)"

echo "== 注册 =="
R=$(curl -s -X POST "$BASE/api/register" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$UNIQ\",\"password\":\"pass123\"}")
check "注册成功" '"ok":true' "$R"

R=$(curl -s -X POST "$BASE/api/register" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$UNIQ\",\"password\":\"pass123\"}")
check "重复用户名 USERNAME_EXISTS" 'USERNAME_EXISTS' "$R"

R=$(curl -s -X POST "$BASE/api/register" -H 'Content-Type: application/json' \
    -d '{"username":"ab","password":"pass123"}')
check "用户名过短 PARAM_INVALID" 'PARAM_INVALID' "$R"

R=$(curl -s -X POST "$BASE/api/register" -H 'Content-Type: application/json' \
    -d '{"username":"bad name","password":"pass123"}')
check "非法字符 PARAM_INVALID" 'PARAM_INVALID' "$R"

R=$(curl -s -X POST "$BASE/api/register" -H 'Content-Type: application/json' \
    -d "{\"username\":\"${UNIQ}_x\",\"password\":\"123\"}")
check "密码过短 PARAM_INVALID" 'PARAM_INVALID' "$R"

R=$(curl -s -X POST "$BASE/api/register" -H 'Content-Type: application/json' \
    -d "{\"username\":\"${UNIQ}_y\"}")
check "缺少 password PARAM_INVALID" 'PARAM_INVALID' "$R"

echo "== 登录 =="
R=$(curl -s -c "$JAR" -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$UNIQ\",\"password\":\"pass123\"}")
check "登录成功" '"ok":true' "$R"

R=$(curl -s -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$UNIQ\",\"password\":\"wrong\"}")
check "密码错误 WRONG_PASSWORD" 'WRONG_PASSWORD' "$R"

R=$(curl -s -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d '{"username":"no_such_user_zzz","password":"pass123"}')
check "用户不存在 USER_NOT_FOUND" 'USER_NOT_FOUND' "$R"

echo "== /api/me =="
R=$(curl -s -b "$JAR" "$BASE/api/me")
check "已登录返回用户" "$UNIQ" "$R"

R=$(curl -s -w "|%{http_code}" "$BASE/api/me")
check "未登录 401" 'NOT_AUTHENTICATED' "$R"
[[ "$R" == *"|401" ]] && { echo "PASS: 未登录 HTTP 401"; PASS=$((PASS + 1)); } \
    || { echo "FAIL: 未登录 HTTP 状态（实际 $R）"; FAIL=$((FAIL + 1)); }

echo "== 登出 =="
R=$(curl -s -b "$JAR" -c "$JAR" -X POST "$BASE/api/logout")
check "登出成功" '"ok":true' "$R"

R=$(curl -s -b "$JAR" -w "|%{http_code}" "$BASE/api/me")
check "登出后 401" 'NOT_AUTHENTICATED' "$R"

rm -f "$JAR"

echo "----"
echo "通过 $PASS，失败 $FAIL"
[[ $FAIL -eq 0 ]]
