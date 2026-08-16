#!/usr/bin/env bash
# tests/api/test_class.sh — 阶段 8 班级功能测试（curl）。
#
# 覆盖：
#   教师建班（幂等）→ 查看班级（含邀请码/成员）→ 重置邀请码
#   学生凭邀请码入班（成功/无效码/重复加入）
#   可见性：未入班学生列表为空；入班学生可见全局题与本班教师题；教师看全部
#   学生访问教师接口 403
#
# 用法：
#   ./tests/api/test_class.sh [base_url]
#   默认 base_url=http://127.0.0.1:8080
set -uo pipefail

BASE="${1:-http://127.0.0.1:8080}"
PASS=0
FAIL=0

check() {
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
    local desc="$1" want="$2" got="$3"
    if [[ "$got" == *"|$want"* ]]; then
        echo "PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "FAIL: $desc (期望 HTTP $want，实际: $got)"
        FAIL=$((FAIL + 1))
    fi
}

TS="$(date +%s%N)"
TEACH="tch_$TS"
STUDENT="stu_$TS"
STUDENT2="stu2_$TS"
JAR_T="$(mktemp)"
JAR_S="$(mktemp)"
JAR_S2="$(mktemp)"

echo "== 准备账号 =="
curl -s -X POST "$BASE/api/register" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$TEACH\",\"password\":\"pass123\"}" > /dev/null
curl -s -X POST "$BASE/api/register" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$STUDENT\",\"password\":\"pass123\"}" > /dev/null
curl -s -X POST "$BASE/api/register" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$STUDENT2\",\"password\":\"pass123\"}" > /dev/null
# 教师与学生都在库中，用 SQL 提升角色（阶段 3 注册固定 student）
mysql -uoj -poj_password oj_vibecoding -e \
    "UPDATE users SET role='teacher' WHERE username='$TEACH';" 2>/dev/null
TID=$(mysql -uoj -poj_password oj_vibecoding -N -e \
    "SELECT id FROM users WHERE username='$TEACH';" 2>/dev/null)
curl -s -c "$JAR_T" -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$TEACH\",\"password\":\"pass123\"}" > /dev/null
curl -s -c "$JAR_S" -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$STUDENT\",\"password\":\"pass123\"}" > /dev/null
curl -s -c "$JAR_S2" -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$STUDENT2\",\"password\":\"pass123\"}" > /dev/null

# 为教师直接插入一道属于他的题目（阶段 8 题目导入接口未实现，先用 SQL 造数）
PROB_TITLE="Teacher Problem $TS"
mysql -uoj -poj_password oj_vibecoding -e \
    "INSERT INTO problems (title, description, sample_in, sample_out, time_limit_ms, memory_limit_mb, difficulty, test_dir, created_by)
     VALUES ('$PROB_TITLE', 'desc', '1\n', '1\n', 1000, 256, 1, '', $TID);" 2>/dev/null

echo "== 教师建班 =="
R=$(curl -s -b "$JAR_T" -X POST "$BASE/api/admin/class" \
    -H 'Content-Type: application/json' -d '{"name":"测试班"}')
check "建班成功" '"ok":true' "$R"
check "含班级 name 字段" '"name"' "$R"
CODE=$(echo "$R" | grep -o '"invite_code":"[A-Z0-9]*"' | cut -d'"' -f4)
if [[ -z "$CODE" ]]; then
    echo "FAIL: 未能解析邀请码 ($R)"
    FAIL=$((FAIL + 1))
    CODE=""
fi

R=$(curl -s -b "$JAR_T" -X POST "$BASE/api/admin/class" \
    -H 'Content-Type: application/json' -d '{"name":"测试班"}')
check "重复建班幂等 ok" '"ok":true' "$R"

echo "== 教师查看班级 =="
R=$(curl -s -b "$JAR_T" "$BASE/api/admin/class")
check "查看班级 ok" '"ok":true' "$R"
check "含邀请码" '"invite_code"' "$R"
check "含 members 字段" '"members"' "$R"

echo "== 重置邀请码 =="
R=$(curl -s -b "$JAR_T" -X POST "$BASE/api/admin/class/invite")
check "重置邀请码 ok" '"ok":true' "$R"
CODE2=$(echo "$R" | grep -o '"invite_code":"[A-Z0-9]*"' | cut -d'"' -f4)
if [[ -n "$CODE2" && "$CODE2" != "$CODE" ]]; then
    echo "PASS: 邀请码已更新"
    PASS=$((PASS + 1))
else
    echo "FAIL: 邀请码未更新 ($CODE -> $CODE2)"
    FAIL=$((FAIL + 1))
fi
CODE="$CODE2"

echo "== 学生入班 =="
R=$(curl -s -b "$JAR_S" -X POST "$BASE/api/class/join" \
    -H 'Content-Type: application/json' -d "{\"invite_code\":\"$CODE\"}")
check "入班成功" '"ok":true' "$R"

R=$(curl -s -b "$JAR_S" -X POST "$BASE/api/class/join" \
    -H 'Content-Type: application/json' -d "{\"invite_code\":\"$CODE\"}")
check "重复加入 ALREADY_JOINED" 'ALREADY_JOINED' "$R"

R=$(curl -s -b "$JAR_S" -X POST "$BASE/api/class/join" \
    -H 'Content-Type: application/json' -d '{"invite_code":"BADCODE"}')
check "无效邀请码 INVITE_CODE_INVALID" 'INVITE_CODE_INVALID' "$R"

echo "== 可见性 =="
R=$(curl -s -b "$JAR_T" "$BASE/api/problems")
check "教师可见全局题" 'A+B Problem' "$R"
check "教师可见自己的题" "$PROB_TITLE" "$R"

# 未入班学生：可见全局题，不可见教师题
R=$(curl -s -b "$JAR_S2" "$BASE/api/problems")
check "未入班学生可见全局题" 'A+B Problem' "$R"
if [[ "$R" == *"$PROB_TITLE"* ]]; then
    echo "FAIL: 未入班学生不应可见教师题 ($PROB_TITLE)"
    FAIL=$((FAIL + 1))
else
    echo "PASS: 未入班学生不可见教师题"
    PASS=$((PASS + 1))
fi

R=$(curl -s -b "$JAR_S" "$BASE/api/problems")
check "入班学生可见全局题" 'A+B Problem' "$R"
check "入班学生可见本班教师题" "$PROB_TITLE" "$R"

echo "== 权限 =="
R=$(curl -s -b "$JAR_S" -w "|%{http_code}" "$BASE/api/admin/class")
check_code "学生访问教师接口 403" 403 "$R"
check "错误码 FORBIDDEN" 'FORBIDDEN' "$R"

R=$(curl -s -b "$JAR_S" -w "|%{http_code}" -X POST "$BASE/api/admin/class/invite")
check_code "学生重置邀请码 403" 403 "$R"

echo "== 清理 =="
mysql -uoj -poj_password oj_vibecoding -e \
    "DELETE FROM problems WHERE title='$PROB_TITLE';
     DELETE FROM users WHERE username IN ('$TEACH','$STUDENT','$STUDENT2');" 2>/dev/null
rm -f "$JAR_T" "$JAR_S" "$JAR_S2"

echo "----"
echo "通过 $PASS，失败 $FAIL"
[[ $FAIL -eq 0 ]]
