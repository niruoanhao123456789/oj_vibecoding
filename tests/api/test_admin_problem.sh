#!/usr/bin/env bash
# tests/api/test_admin_problem.sh — 阶段 8 题目管理测试（curl）。
#
# 覆盖：
#   管理员导入 → 全局题（created_by NULL），教师/已入班学生可见
#   教师导入 → 本班题（created_by = 教师）
#   修改：管理员可改任意题；教师仅能改自己的题（改别人的题 400）
#   删除：管理员可删任意题；教师仅能删自己的题
#   越权：学生访问题目管理接口 403
#
# 用法：
#   ./tests/api/test_admin_problem.sh [base_url]
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

TS="$(date +%s%N)"
JAR_A="$(mktemp)"
JAR_T="$(mktemp)"
JAR_S="$(mktemp)"
TITLE_G="全局题 $TS"
TITLE_T="教师题 $TS"

# 登录管理员
curl -s -c "$JAR_A" -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d '{"username":"admin","password":"admin123"}' >/dev/null

# 准备一个教师账号（管理员建号）
TEACH="ptea_$TS"
R=$(curl -s -b "$JAR_A" -X POST "$BASE/api/admin/users" \
    -H 'Content-Type: application/json' \
    -d "{\"username\":\"$TEACH\",\"password\":\"pass123\",\"role\":\"teacher\"}")
TEACH_ID=$(echo "$R" | grep -o '"id":[0-9]*' | head -1 | cut -d: -f2)
curl -s -c "$JAR_T" -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$TEACH\",\"password\":\"pass123\"}" >/dev/null

# 准备一个学生账号并让其入教师班
STU="pstu_$TS"
curl -s -X POST "$BASE/api/register" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$STU\",\"password\":\"pass123\"}" >/dev/null
curl -s -c "$JAR_S" -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$STU\",\"password\":\"pass123\"}" >/dev/null

GJSON=$(python3 -c "import json,sys;print(json.dumps({'title':sys.argv[1],'description':'sum','sample_in':'1 2\\n','sample_out':'3\\n','test_cases':[{'input':'1 2\\n','output':'3\\n'}]}))" "$TITLE_G")
TJSON=$(python3 -c "import json,sys;print(json.dumps({'title':sys.argv[1],'description':'sum','sample_in':'1 2\\n','sample_out':'3\\n','test_cases':[{'input':'1 2\\n','output':'3\\n'}]}))" "$TITLE_T")

echo "== 管理员导入（全局题） =="
R=$(curl -s -b "$JAR_A" -X POST "$BASE/api/admin/problems/import" \
    -H 'Content-Type: application/json' -d "$GJSON")
check "管理员导入成功" '"ok":true' "$R"
GID=$(echo "$R" | grep -o '"id":[0-9]*' | head -1 | cut -d: -f2)
if [[ "$GID" =~ ^[0-9]+$ ]]; then
    echo "PASS: 返回题目 id ($GID)"
    PASS=$((PASS + 1))
else
    echo "FAIL: 返回题目 id (实际: $GID)"
    FAIL=$((FAIL + 1))
fi

# 管理员建班并让学生加入，验证全局题可见
curl -s -b "$JAR_A" -X POST "$BASE/api/admin/class" -H 'Content-Type: application/json' \
    -d '{"name":"管理员班"}' >/dev/null
R=$(curl -s -b "$JAR_A" -X POST "$BASE/api/admin/class" -H 'Content-Type: application/json' -d '{}')
CODE=$(echo "$R" | grep -o '"invite_code":"[A-Z0-9]*"' | cut -d'"' -f4)
curl -s -b "$JAR_S" -X POST "$BASE/api/class/join" -H 'Content-Type: application/json' \
    -d "{\"invite_code\":\"$CODE\"}" >/dev/null
R=$(curl -s -b "$JAR_S" "$BASE/api/problems")
VISIBLE=$(echo "$R" | python3 -c "
import json,sys
try:
    d=json.load(sys.stdin)['data']['problems']
    print('true' if any(p['title']=='$TITLE_G' for p in d) else 'false')
except Exception:
    print('false')
")
check "已入班学生可见全局题" 'true' "$VISIBLE"

echo "== 教师导入（本班题） =="
R=$(curl -s -b "$JAR_T" -X POST "$BASE/api/admin/problems/import" \
    -H 'Content-Type: application/json' -d "$TJSON")
check "教师导入成功" '"ok":true' "$R"
TID=$(echo "$R" | grep -o '"id":[0-9]*' | head -1 | cut -d: -f2)

echo "== 修改：越权与权限 =="
# 教师改管理员的全局题 → 拒绝
R=$(curl -s -b "$JAR_T" -X PUT "$BASE/api/admin/problems/$GID" \
    -H 'Content-Type: application/json' -d "$TJSON")
check "教师改他人题被拒" '"code":"PARAM_INVALID"' "$R"

# 教师改自己的题 → 成功
R=$(curl -s -b "$JAR_T" -X PUT "$BASE/api/admin/problems/$TID" \
    -H 'Content-Type: application/json' -d "$TJSON")
check "教师改自己题成功" '"ok":true' "$R"

# 管理员改教师的题 → 成功
R=$(curl -s -b "$JAR_A" -X PUT "$BASE/api/admin/problems/$TID" \
    -H 'Content-Type: application/json' -d "$TJSON")
check "管理员改任意题成功" '"ok":true' "$R"

echo "== 删除：越权与权限 =="
R=$(curl -s -b "$JAR_T" -X DELETE "$BASE/api/admin/problems/$GID")
check "教师删他人题被拒" '"code":"PARAM_INVALID"' "$R"

R=$(curl -s -b "$JAR_T" -X DELETE "$BASE/api/admin/problems/$TID")
check "教师删自己题成功" '"ok":true' "$R"

R=$(curl -s -b "$JAR_A" -X DELETE "$BASE/api/admin/problems/$GID")
check "管理员删任意题成功" '"ok":true' "$R"

R=$(curl -s -b "$JAR_A" -X DELETE "$BASE/api/admin/problems/$GID")
check "删除不存在的题 400" '"code":"PARAM_INVALID"' "$R"

echo "== 越权：学生访问题目管理 403 =="
R=$(curl -s -o /dev/null -w "%{http_code}" -b "$JAR_S" -X POST \
    "$BASE/api/admin/problems/import")
check "学生导入 403" '403' "|$R|"
R=$(curl -s -o /dev/null -w "%{http_code}" -b "$JAR_S" \
    -X DELETE "$BASE/api/admin/problems/1")
check "学生删除 403" '403' "|$R|"

echo "== 清理 =="
# 删除学生账号与教师账号
for u in "$STU" "$TEACH"; do
    ID=$(curl -s -b "$JAR_A" "$BASE/api/admin/users" | \
        python3 -c "import json,sys;d=json.load(sys.stdin)['data']['users'];print(next((str(x['id']) for x in d if x['username']=='$u'),''))" 2>/dev/null)
    if [[ -n "$ID" ]]; then
        curl -s -b "$JAR_A" -X DELETE "$BASE/api/admin/users/$ID" >/dev/null
    fi
done

echo ""
echo "----"
echo "通过 $PASS，失败 $FAIL"
rm -f "$JAR_A" "$JAR_T" "$JAR_S"
[[ $FAIL -eq 0 ]]
