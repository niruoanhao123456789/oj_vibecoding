#!/usr/bin/env bash
# tests/api/test_admin_users.sh — 阶段 8 管理员用户管理测试（curl）。
#
# 覆盖：
#   管理员：列表/新增/改角色/禁用启用/删除
#   教师注册邀请码：正确→teacher、错误→400、留空→student
#   保护规则：自降级/自禁用/自删除被拒；内建 admin 不可降级/删除；
#   有班级的教师降级/删除 → 班级一并删除
#   学生访问管理员接口 403
#
# 用法：
#   ./tests/api/test_admin_users.sh [base_url]
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
ADMIN="admin"
ADMIN_PASS="admin123"
JAR_A="$(mktemp)"
JAR_S="$(mktemp)"
TEACH_USER="tch_$TS"
STU_USER="stu_$TS"
NEW_USER="new_$TS"

# 登录管理员
R=$(curl -s -c "$JAR_A" -X POST "$BASE/api/login" \
    -H 'Content-Type: application/json' \
    -d "{\"username\":\"$ADMIN\",\"password\":\"$ADMIN_PASS\"}")
check "管理员登录" '"ok":true' "$R"

echo "== 教师注册邀请码 =="
R=$(curl -s -X POST "$BASE/api/register" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$TEACH_USER\",\"password\":\"pass123\",\"teacher_code\":\"TEACH-2026\"}")
check "正确邀请码注册为教师" '"role":"teacher"' "$R"

R=$(curl -s -X POST "$BASE/api/register" -H 'Content-Type: application/json' \
    -d "{\"username\":\"bad_$TS\",\"password\":\"pass123\",\"teacher_code\":\"WRONG\"}")
check "错误邀请码返回 400" '"code":"TEACHER_CODE_INVALID"' "$R"

R=$(curl -s -X POST "$BASE/api/register" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$STU_USER\",\"password\":\"pass123\"}")
check "留空邀请码注册为学生" '"role":"student"' "$R"

echo "== 用户列表 =="
R=$(curl -s -b "$JAR_A" "$BASE/api/admin/users")
check "列表含新注册教师" "\"$TEACH_USER\"" "$R"
check "列表含新注册学生" "\"$STU_USER\"" "$R"
check "列表含 has_class 字段" '"has_class"' "$R"

echo "== 新增用户 =="
R=$(curl -s -b "$JAR_A" -X POST "$BASE/api/admin/users" \
    -H 'Content-Type: application/json' \
    -d "{\"username\":\"$NEW_USER\",\"password\":\"pass123\",\"role\":\"teacher\"}")
check "管理员建教师账号" '"role":"teacher"' "$R"
NEW_ID=$(echo "$R" | grep -o '"id":[0-9]*' | head -1 | cut -d: -f2)
if [[ "$NEW_ID" =~ ^[0-9]+$ ]]; then
    echo "PASS: 返回新用户 id ($NEW_ID)"
    PASS=$((PASS + 1))
else
    echo "FAIL: 返回新用户 id (实际: $NEW_ID)"
    FAIL=$((FAIL + 1))
fi

R=$(curl -s -b "$JAR_A" -X POST "$BASE/api/admin/users" \
    -H 'Content-Type: application/json' \
    -d "{\"username\":\"$ADMIN\",\"password\":\"pass123\",\"role\":\"student\"}")
check "重复用户名 409" '"code":"USERNAME_EXISTS"' "$R"

R=$(curl -s -b "$JAR_A" -X POST "$BASE/api/admin/users" \
    -H 'Content-Type: application/json' \
    -d "{\"username\":\"x_$TS\",\"password\":\"pass123\",\"role\":\"root\"}")
check "非法角色 400" '"code":"PARAM_INVALID"' "$R"

echo "== 修改角色 / 状态 =="
R=$(curl -s -b "$JAR_A" -X PUT "$BASE/api/admin/users/$NEW_ID" \
    -H 'Content-Type: application/json' -d '{"role":"student"}')
check "改角色为 student" '"role":"student"' "$R"

R=$(curl -s -b "$JAR_A" -X PUT "$BASE/api/admin/users/$NEW_ID" \
    -H 'Content-Type: application/json' -d '{"status":0}')
check "禁用用户" '"status":0' "$R"

R=$(curl -s -b "$JAR_A" -X PUT "$BASE/api/admin/users/$NEW_ID" \
    -H 'Content-Type: application/json' -d '{"status":1}')
check "启用用户" '"status":1' "$R"

echo "== 保护规则 =="
R=$(curl -s -b "$JAR_A" -X PUT "$BASE/api/admin/users/1" \
    -H 'Content-Type: application/json' -d '{"role":"student"}')
check "内建 admin 不可降级" '"code":"CANNOT_MODIFY_SELF"' "$R"

R=$(curl -s -b "$JAR_A" -X PUT "$BASE/api/admin/users/1" \
    -H 'Content-Type: application/json' -d '{"status":0}')
check "admin 不可禁用自己" '"code":"CANNOT_MODIFY_SELF"' "$R"

R=$(curl -s -b "$JAR_A" -X DELETE "$BASE/api/admin/users/1")
check "admin 不可删除自己" '"code":"CANNOT_MODIFY_SELF"' "$R"

echo "== 有班级的教师：降级/删除联动删除班级 =="
# 让新用户（当前为 student）重新升级为 teacher 并建班
curl -s -b "$JAR_A" -X PUT "$BASE/api/admin/users/$NEW_ID" \
    -H 'Content-Type: application/json' -d '{"role":"teacher"}' >/dev/null
JAR_N="$(mktemp)"
curl -s -c "$JAR_N" -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$NEW_USER\",\"password\":\"pass123\"}" >/dev/null
R=$(curl -s -b "$JAR_N" -X POST "$BASE/api/admin/class" \
    -H 'Content-Type: application/json' -d '{"name":"测试班"}')
check "教师建班" '"invite_code"' "$R"

R=$(curl -s -b "$JAR_A" "$BASE/api/admin/users")
check "列表标记该用户有班级" "\"$NEW_USER\"" "$R"
HAS=$(echo "$R" | grep -o "has_class\":true" | head -1)
check "has_class 为 true" 'true' "$HAS"

R=$(curl -s -b "$JAR_A" -X PUT "$BASE/api/admin/users/$NEW_ID" \
    -H 'Content-Type: application/json' -d '{"role":"student"}')
check "有班级教师降级成功" '"role":"student"' "$R"
R=$(curl -s -b "$JAR_A" "$BASE/api/admin/users")
HAS2=$(echo "$R" | python3 -c "
import json,sys
d=json.load(sys.stdin)['data']['users']
u=next((x for x in d if x['username']=='$NEW_USER'),None)
print('true' if u and u.get('has_class') else 'false')
")
check "降级后班级已删除" 'false' "$HAS2"

# 再次升级并建班，然后删除验证班级级联
curl -s -b "$JAR_A" -X PUT "$BASE/api/admin/users/$NEW_ID" \
    -H 'Content-Type: application/json' -d '{"role":"teacher"}' >/dev/null
curl -s -b "$JAR_N" -X POST "$BASE/api/admin/class" \
    -H 'Content-Type: application/json' -d '{"name":"测试班2"}' >/dev/null
R=$(curl -s -b "$JAR_A" -X DELETE "$BASE/api/admin/users/$NEW_ID")
check "有班级教师删除成功" '"ok":true' "$R"
R=$(curl -s -b "$JAR_A" "$BASE/api/admin/users")
if [[ "$R" == *"$NEW_USER"* ]]; then
    echo "FAIL: 删除后用户应不存在"
    FAIL=$((FAIL + 1))
else
    echo "PASS: 删除后用户已消失"
    PASS=$((PASS + 1))
fi

echo "== 权限 =="
JAR_S2="$(mktemp)"
curl -s -c "$JAR_S2" -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$STU_USER\",\"password\":\"pass123\"}" >/dev/null
R=$(curl -s -o /dev/null -w "%{http_code}" -b "$JAR_S2" "$BASE/api/admin/users")
check_code "学生访问用户列表 403" 403 "|$R|"

echo "== 教师邀请码配置 =="
R=$(curl -s -b "$JAR_A" "$BASE/api/admin/config")
check "读取教师邀请码" '"teacher_invite_code"' "$R"
R=$(curl -s -b "$JAR_A" -X PUT "$BASE/api/admin/config" \
    -H 'Content-Type: application/json' -d '{"teacher_invite_code":"NEW-CODE"}')
check "更新教师邀请码" '"NEW-CODE"' "$R"
R=$(curl -s -b "$JAR_A" -X PUT "$BASE/api/admin/config" \
    -H 'Content-Type: application/json' -d '{"teacher_invite_code":"TEACH-2026"}')
check "还原教师邀请码" '"TEACH-2026"' "$R"

echo "== 清理 =="
# 删除本测试创建的账号（保留 admin）
for u in "$TEACH_USER" "$STU_USER" "bad_$TS"; do
    ID=$(curl -s -b "$JAR_A" "$BASE/api/admin/users" | \
        python3 -c "import json,sys;d=json.load(sys.stdin)['data']['users'];print(next((str(x['id']) for x in d if x['username']=='$u'),''))" 2>/dev/null)
    if [[ -n "$ID" ]]; then
        curl -s -b "$JAR_A" -X DELETE "$BASE/api/admin/users/$ID" >/dev/null
    fi
done

echo ""
echo "----"
echo "通过 $PASS，失败 $FAIL"
rm -f "$JAR_A" "$JAR_S" "$JAR_N" "$JAR_S2"
[[ $FAIL -eq 0 ]]
