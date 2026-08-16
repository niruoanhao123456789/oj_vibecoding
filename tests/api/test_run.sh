#!/usr/bin/env bash
# tests/api/test_run.sh — 阶段 9 自测运行接口测试（curl）。
#
# 覆盖：
#   POST /api/problems/:id/run：AC / WA / CE 三类自测
#   用户自定义用例（增删改场景的请求形态）与「无期望输出仅显示实际输出」
#   运行不算正式提交：前后 submissions 计数不变
#   不可见题目 404、未登录 401
#
# 用法：
#   ./tests/api/test_run.sh [base_url]
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
JAR_A="$(mktemp)"
JAR_S="$(mktemp)"
STU="run_stu_$TS"
AC_CODE=$'#include <iostream>\nint main(){ long long a,b; std::cin>>a>>b; std::cout<<a+b<<"\\n"; return 0; }'
WA_CODE=$'#include <iostream>\nint main(){ long long a,b; std::cin>>a>>b; std::cout<<a-b<<"\\n"; return 0; }'
CE_CODE='int main() { this is not valid c++ }'

# 管理员登录
curl -s -c "$JAR_A" -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d '{"username":"admin","password":"admin123"}' >/dev/null

# 注册学生并登录（不入班，用于不可见题目）
curl -s -X POST "$BASE/api/register" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$STU\",\"password\":\"pass123\"}" >/dev/null
curl -s -c "$JAR_S" -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$STU\",\"password\":\"pass123\"}" >/dev/null

# 导入专用题目（A+B，样例明文 1 2 → 3）
TITLE="RUN $TS"
GJSON=$(python3 -c "import json,sys;print(json.dumps({'title':sys.argv[1],'description':'a+b','sample_in':'1 2\\n','sample_out':'3\\n','time_limit_ms':1000,'memory_limit_mb':256,'test_cases':[{'input':'1 2\\n','output':'3\\n'}]}))" "$TITLE")
R=$(curl -s -b "$JAR_A" -X POST "$BASE/api/admin/problems/import" \
    -H 'Content-Type: application/json' -d "$GJSON")
PID=$(echo "$R" | python3 -c "import json,sys;print(json.loads(sys.argv[1])['data']['id'])" "$R" 2>/dev/null)
if [[ -z "$PID" || "$PID" == "None" ]]; then
    echo "FAIL: 导入测试题目失败 ($R)"
    exit 1
fi
echo "测试题目 id=$PID (title=$TITLE)"

# 提交运行请求；打印响应 JSON
run() {
    # run <jar> <code> <cases-json-file>
    local body
    body=$(python3 -c "import json,sys;print(json.dumps({'language':'cpp','code':open(sys.argv[1]).read(),'test_cases':json.loads(open(sys.argv[2]).read())}))" "$2" "$3")
    curl -s -b "$1" -X POST "$BASE/api/problems/$PID/run" \
        -H 'Content-Type: application/json' -d "$body"
}

# 写代码与用例到临时文件（避免 shell 转义地狱）
mkcode() {
    printf '%s' "$1" > "$2"
}
mkjson() {
    # mkjson <文件> <python 表达式构造 test_cases>
    python3 -c "import json,sys;print(json.dumps($1))" > "$2"
}

CF=$(mktemp)
BODYF=$(mktemp)
CUSTOM=$(mktemp)

echo "== 自测运行：AC（样例用例） =="
mkcode "$AC_CODE" "$CF"
mkjson "[{'input':'1 2\n','expected':'3\n'},{'input':'100 -50\n','expected':'50\n'}]" "$BODYF"
R=$(run "$JAR_A" "$CF" "$BODYF")
check "AC 总体判定 AC" '"overall":"AC"' "$R"
check "用例 1 AC" '"verdict":"AC"' "$R"

echo "== 自测运行：WA =="
mkcode "$WA_CODE" "$CF"
mkjson "[{'input':'1 2\n','expected':'3\n'}]" "$BODYF"
R=$(run "$JAR_A" "$CF" "$BODYF")
check "WA 总体判定 WA" '"overall":"WA"' "$R"
check "WA 含实际输出" '"actual":' "$R"
check "WA 期望输出保留" '"expected":"3\n"' "$R"

echo "== 自测运行：CE =="
mkcode "$CE_CODE" "$CF"
mkjson "[{'input':'1\n','expected':'1\n'}]" "$BODYF"
R=$(run "$JAR_A" "$CF" "$BODYF")
check "CE 总体判定 COMPILE_ERROR" '"overall":"COMPILE_ERROR"' "$R"
check "CE 编译 ok=false" '"ok":false' "$R"
check "CE 含编译输出" '"output":"' "$R"

echo "== 自定义用例（与样例不同，用户新增） =="
mkcode "$AC_CODE" "$CF"
mkjson "[{'input':'7 8\n','expected':'15\n'}]" "$BODYF"
R=$(run "$JAR_A" "$CF" "$BODYF")
check "自定义用例 AC" '"overall":"AC"' "$R"

echo "== 无期望输出：仅显示实际输出，verdict=NONE =="
mkcode "$AC_CODE" "$CF"
mkjson "[{'input':'9 9\n'}]" "$BODYF"
R=$(run "$JAR_A" "$CF" "$BODYF")
check "无期望输出总体 NONE" '"overall":"NONE"' "$R"
check "无期望输出用例 NONE" '"verdict":"NONE"' "$R"
check "仍给出实际输出" '"actual":"18\n"' "$R"

echo "== 运行不算正式提交：submissions 计数不变 =="
CNT_BEFORE=$(mysql -uoj -poj_password oj_vibecoding -N -e \
    "SELECT COUNT(*) FROM submissions WHERE problem_id=$PID;" 2>/dev/null)
mkcode "$AC_CODE" "$CF"
mkjson "[{'input':'1 2\n','expected':'3\n'}]" "$BODYF"
run "$JAR_A" "$CF" "$BODYF" > /dev/null
CNT_AFTER=$(mysql -uoj -poj_password oj_vibecoding -N -e \
    "SELECT COUNT(*) FROM submissions WHERE problem_id=$PID;" 2>/dev/null)
check "运行前后提交数一致（0）" '0' "before=$CNT_BEFORE after=$CNT_AFTER"

echo "== 参数与权限 =="
R=$(curl -s -b "$JAR_A" -X POST "$BASE/api/problems/$PID/run" \
    -H 'Content-Type: application/json' \
    -d '{"language":"java","code":"int main(){}","test_cases":[]}')
check "非法语言 400 PARAM_INVALID" 'PARAM_INVALID' "$R"

R=$(curl -s -b "$JAR_A" -X POST "$BASE/api/problems/$PID/run" \
    -H 'Content-Type: application/json' \
    -d '{"language":"cpp","code":"   ","test_cases":[]}')
check "空白代码 400 PARAM_INVALID" 'PARAM_INVALID' "$R"

R=$(curl -s -b "$JAR_S" -w "|%{http_code}" -X POST "$BASE/api/problems/$PID/run" \
    -H 'Content-Type: application/json' \
    -d '{"language":"cpp","code":"int main(){}","test_cases":[]}')
check_code "未入班学生可运行全局题 HTTP 200" 200 "$R"
check "未入班学生可运行全局题" '"ok":true' "$R"

R=$(curl -s -o /dev/null -w "|%{http_code}" -X POST "$BASE/api/problems/$PID/run" \
    -H 'Content-Type: application/json' \
    -d '{"language":"cpp","code":"int main(){}","test_cases":[]}')
check_code "未登录运行 401" 401 "$R"

echo "== 清理 =="
mysql -uoj -poj_password oj_vibecoding -e \
    "DELETE FROM submissions WHERE problem_id=$PID;
     DELETE FROM problems WHERE id=$PID;
     DELETE FROM users WHERE username='$STU';" 2>/dev/null
rm -f "$JAR_A" "$JAR_S" "$CF" "$BODYF" "$CUSTOM"

echo ""
echo "----"
echo "通过 $PASS，失败 $FAIL"
[[ $FAIL -eq 0 ]]
