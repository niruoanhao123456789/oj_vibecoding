#!/usr/bin/env bash
# tests/e2e/run_smoke.sh — 阶段 9 端到端冒烟测试（curl）。
#
# 全流程自动验证（SPEC 11 验收标准主链路）：
#   注册教师 → 建班 → 导入题目 → 学生注册 → 凭邀请码入班 →
#   选题（可见性）→ 提交 AC/WA → 轮询判题 → 历史列表 →
#   个人统计（聚合）→ 教师统计 一键跑通。
#
# 用法：
#   ./tests/e2e/run_smoke.sh [base_url]
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
TEACH="e2e_tch_$TS"
STU="e2e_stu_$TS"
JAR_T="$(mktemp)"
JAR_S="$(mktemp)"
JAR_A="$(mktemp)"
TITLE="E2E A+B $TS"

echo "== 1. 注册教师（填写教师邀请码） =="
R=$(curl -s -X POST "$BASE/api/register" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$TEACH\",\"password\":\"pass123\",\"teacher_code\":\"TEACH-2026\"}")
check "教师注册成功" '"role":"teacher"' "$R"

curl -s -c "$JAR_T" -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$TEACH\",\"password\":\"pass123\"}" >/dev/null

echo "== 2. 教师建班 → 获取邀请码 =="
R=$(curl -s -b "$JAR_T" -X POST "$BASE/api/admin/class" \
    -H 'Content-Type: application/json' -d '{"name":"E2E 冒烟班"}')
check "教师建班成功" '"invite_code"' "$R"
CODE=$(echo "$R" | python3 -c "import json,sys;print(json.loads(sys.argv[1])['data']['class']['invite_code'])" "$R" 2>/dev/null)
if [[ -z "$CODE" || "$CODE" == "None" ]]; then
    echo "FAIL: 无法解析邀请码 ($R)"
    exit 1
fi

echo "== 3. 教师导入题目（内联 test_cases，双测试点） =="
GJSON=$(python3 -c "import json,sys;print(json.dumps({'title':sys.argv[1],'description':'读入两整数输出和','sample_in':'1 2\\n','sample_out':'3\\n','time_limit_ms':1000,'memory_limit_mb':256,'difficulty':1,'test_cases':[{'input':'1 2\\n','output':'3\\n'},{'input':'100 -50\\n','output':'50\\n'}]}))" "$TITLE")
R=$(curl -s -b "$JAR_T" -X POST "$BASE/api/admin/problems/import" \
    -H 'Content-Type: application/json' -d "$GJSON")
check "教师导入题目成功" '"ok":true' "$R"
PID=$(echo "$R" | python3 -c "import json,sys;print(json.loads(sys.argv[1])['data']['id'])" "$R" 2>/dev/null)
if [[ -z "$PID" || "$PID" == "None" ]]; then
    echo "FAIL: 无法解析题目 id ($R)"
    exit 1
fi

echo "== 4. 学生注册、登录、凭邀请码入班 =="
R=$(curl -s -X POST "$BASE/api/register" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$STU\",\"password\":\"pass123\"}")
check "学生注册成功" '"role":"student"' "$R"
curl -s -c "$JAR_S" -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$STU\",\"password\":\"pass123\"}" >/dev/null
R=$(curl -s -b "$JAR_S" -X POST "$BASE/api/class/join" \
    -H 'Content-Type: application/json' -d "{\"invite_code\":\"$CODE\"}")
check "学生凭邀请码入班成功" '"ok":true' "$R"

echo "== 5. 选题：学生可见教师题目 =="
R=$(curl -s -b "$JAR_S" "$BASE/api/problems")
check "学生题目列表含教师题" "$TITLE" "$R"

echo "== 6. 提交 AC 代码 → 轮询判题 =="
AC_CODE=$'#include <iostream>\nint main(){ long long a,b; std::cin>>a>>b; std::cout<<a+b<<"\\n"; return 0; }'
BODY=$(python3 -c "import json,sys;print(json.dumps({'problem_id':int(sys.argv[1]),'language':'cpp','code':sys.argv[2]}))" "$PID" "$AC_CODE")
R=$(curl -s -b "$JAR_S" -X POST "$BASE/api/submissions" \
    -H 'Content-Type: application/json' -d "$BODY")
check "提交返回 PENDING" '"status":"PENDING"' "$R"
SID_AC=$(echo "$R" | python3 -c "import json,sys;print(json.loads(sys.argv[1])['data']['id'])" "$R" 2>/dev/null)

ST=""
for _ in $(seq 1 400); do
    R=$(curl -s -b "$JAR_S" "$BASE/api/submissions/$SID_AC")
    ST=$(echo "$R" | python3 -c "import json,sys;print(json.loads(sys.argv[1])['data']['submission']['status'])" "$R" 2>/dev/null)
    case "$ST" in
        AC|WA|RE|TLE|MLE|COMPILE_ERROR|COMPILE_TIMEOUT|SYSTEM_ERROR) break ;;
        *) sleep 0.1 ;;
    esac
done
check "AC 提交轮询到 AC" 'AC' "状态=$ST"

echo "== 7. 提交 WA 代码 → 轮询判题 =="
WA_CODE=$'#include <iostream>\nint main(){ long long a,b; std::cin>>a>>b; std::cout<<a-b<<"\\n"; return 0; }'
BODY=$(python3 -c "import json,sys;print(json.dumps({'problem_id':int(sys.argv[1]),'language':'cpp','code':sys.argv[2]}))" "$PID" "$WA_CODE")
R=$(curl -s -b "$JAR_S" -X POST "$BASE/api/submissions" \
    -H 'Content-Type: application/json' -d "$BODY")
SID_WA=$(echo "$R" | python3 -c "import json,sys;print(json.loads(sys.argv[1])['data']['id'])" "$R" 2>/dev/null)
ST=""
for _ in $(seq 1 400); do
    R=$(curl -s -b "$JAR_S" "$BASE/api/submissions/$SID_WA")
    ST=$(echo "$R" | python3 -c "import json,sys;print(json.loads(sys.argv[1])['data']['submission']['status'])" "$R" 2>/dev/null)
    case "$ST" in
        AC|WA|RE|TLE|MLE|COMPILE_ERROR|COMPILE_TIMEOUT|SYSTEM_ERROR) break ;;
        *) sleep 0.1 ;;
    esac
done
check "WA 提交轮询到 WA" 'WA' "状态=$ST"

echo "== 8. 提交历史 =="
R=$(curl -s -b "$JAR_S" "$BASE/api/submissions")
check "历史含 AC 提交" "\"id\":$SID_AC" "$R"
check "历史含 WA 提交" "\"id\":$SID_WA" "$R"

echo "== 9. 个人统计（由 GET /api/submissions 聚合：1 AC / 1 WA，通过率 50%） =="
R=$(curl -s -b "$JAR_S" "$BASE/api/submissions")
STATS=$(echo "$R" | python3 -c "
import json,sys
d=json.loads(sys.argv[1])['data']['submissions']
mine=[s for s in d if s['id'] in ($SID_AC,$SID_WA)]
total=len(mine); ac=sum(1 for s in mine if s['status']=='AC')
print(f'total={total} ac={ac} rate={round(ac*100/total) if total else 0}')
" "$R" 2>/dev/null)
check "个人提交总数 = 2" 'total=2' "$STATS"
check "个人 AC 数 = 1" 'ac=1' "$STATS"
check "个人通过率 = 50" 'rate=50' "$STATS"

echo "== 10. 教师统计（问题 submit_count=2, ac_count=1, pass_rate=50） =="
R=$(curl -s -b "$JAR_T" "$BASE/api/admin/stats")
check "统计含题目标题" "$TITLE" "$R"
check "提交数 = 2" '"submit_count":2' "$R"
check "AC 数 = 1" '"ac_count":1' "$R"
check "通过率 = 50" '"pass_rate":50' "$R"
check "学生统计含该学生" "$STU" "$R"

echo "== 11. 教师 CSV 导出 =="
R=$(curl -s -b "$JAR_T" "$BASE/api/admin/submissions/export.csv?problem_id=$PID")
check "CSV 含表头" 'problem_title' "$R"
check "CSV 含 AC 记录" ',AC,' "$R"
check "CSV 含 WA 记录" ',WA,' "$R"

echo "== 12. 学生无权访问管理接口（403） =="
R=$(curl -s -o /dev/null -w "%{http_code}" -b "$JAR_S" "$BASE/api/admin/stats")
check "学生访问统计 403" '403' "HTTP=$R"

echo "== 清理 =="
# 删除测试题目与提交
curl -s -b "$JAR_T" -X DELETE "$BASE/api/admin/problems/$PID" >/dev/null
# 管理员登录删除测试账号（含教师班级级联）
curl -s -c "$JAR_A" -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d '{"username":"admin","password":"admin123"}' >/dev/null
for u in "$TEACH" "$STU"; do
    ID=$(curl -s -b "$JAR_A" "$BASE/api/admin/users" | \
        python3 -c "import json,sys;d=json.load(sys.stdin)['data']['users'];print(next((str(x['id']) for x in d if x['username']=='$u'),''))" 2>/dev/null)
    if [[ -n "$ID" ]]; then
        curl -s -b "$JAR_A" -X DELETE "$BASE/api/admin/users/$ID" >/dev/null
    fi
done
rm -f "$JAR_T" "$JAR_S" "$JAR_A"

echo ""
echo "----"
echo "通过 $PASS，失败 $FAIL"
[[ $FAIL -eq 0 ]]
