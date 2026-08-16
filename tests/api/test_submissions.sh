#!/usr/bin/env bash
# tests/api/test_submissions.sh — 阶段 9 提交与判题接口测试（curl）。
#
# 覆盖：
#   POST /api/submissions：AC / WA / CE 三类提交（经导入的专用题目，结果确定）
#     非法语言、空代码、超长代码、不可见题目 → 明确错误码
#   GET /api/submissions/:id：轮询观察到 PENDING → … → 终态，含耗时/内存/错误信息
#   GET /api/submissions：历史列表（本人提交按 id 倒序）
#   POST /api/admin/submissions/:id/rejudge：重判回到 PENDING → 再次出终态
#   权限：未登录 401；学生不可查看他人提交 404
#
# 用法：
#   ./tests/api/test_submissions.sh [base_url]
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
STU="sub_stu_$TS"

# 管理员登录
curl -s -c "$JAR_A" -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d '{"username":"admin","password":"admin123"}' >/dev/null

# 注册学生并登录（不入班，用于不可见题目与越权测试）
curl -s -X POST "$BASE/api/register" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$STU\",\"password\":\"pass123\"}" >/dev/null
curl -s -c "$JAR_S" -X POST "$BASE/api/login" -H 'Content-Type: application/json' \
    -d "{\"username\":\"$STU\",\"password\":\"pass123\"}" >/dev/null

# 专用题目：A+B，两个隐藏测试点（确定性判定 AC/WA）
TITLE="SBT $TS"
GJSON=$(python3 -c "import json,sys;print(json.dumps({'title':sys.argv[1],'description':'a+b','sample_in':'1 2\\n','sample_out':'3\\n','time_limit_ms':1000,'memory_limit_mb':256,'test_cases':[{'input':'1 2\\n','output':'3\\n'},{'input':'100 -50\\n','output':'50\\n'}]}))" "$TITLE")
R=$(curl -s -b "$JAR_A" -X POST "$BASE/api/admin/problems/import" \
    -H 'Content-Type: application/json' -d "$GJSON")
PID=$(echo "$R" | python3 -c "import json,sys;print(json.loads(sys.argv[1])['data']['id'])" "$R" 2>/dev/null)
if [[ -z "$PID" || "$PID" == "None" ]]; then
    echo "FAIL: 导入测试题目失败 ($R)"
    exit 1
fi
echo "测试题目 id=$PID (title=$TITLE)"

# 构造提交请求体并提交；打印响应 JSON
submit() {
    # submit <jar> <language> <code>
    local body
    body=$(python3 -c "import json,sys;print(json.dumps({'problem_id':int(sys.argv[1]),'language':sys.argv[2],'code':sys.argv[3]}))" "$PID" "$2" "$3")
    curl -s -b "$1" -X POST "$BASE/api/submissions" \
        -H 'Content-Type: application/json' -d "$body"
}

# 轮询到终态（PENDING/COMPILING/RUNNING 视为进行中）
poll_status() {
    # poll_status <jar> <sid>  → 输出终态字符串；超时输出 TIMEOUT
    local jar="$1" sid="$2"
    for _ in $(seq 1 400); do
        local R
        R=$(curl -s -b "$jar" "$BASE/api/submissions/$sid")
        local st
        st=$(echo "$R" | python3 -c "import json,sys;print(json.loads(sys.argv[1])['data']['submission']['status'])" "$R" 2>/dev/null)
        case "$st" in
            AC|WA|RE|TLE|MLE|COMPILE_ERROR|COMPILE_TIMEOUT|SYSTEM_ERROR)
                echo "$st"
                return
                ;;
        esac
        sleep 0.1
    done
    echo "TIMEOUT"
}

AC_CODE=$'#include <iostream>\nint main(){ long long a,b; std::cin>>a>>b; std::cout<<a+b<<"\\n"; return 0; }'
WA_CODE=$'#include <iostream>\nint main(){ long long a,b; std::cin>>a>>b; std::cout<<a-b<<"\\n"; return 0; }'
CE_CODE='int main() { this is not valid c++ }'

echo "== POST /api/submissions：AC 提交与轮询 =="
R=$(submit "$JAR_A" cpp "$AC_CODE")
check "提交返回 PENDING" '"status":"PENDING"' "$R"
SID=$(echo "$R" | python3 -c "import json,sys;print(json.loads(sys.argv[1])['data']['id'])" "$R" 2>/dev/null)
if [[ -z "$SID" || "$SID" == "None" ]]; then
    echo "FAIL: 无法解析提交 id ($R)"
    exit 1
fi

ST=$(poll_status "$JAR_A" "$SID")
check "AC 提交最终判定 AC" 'AC' "状态=$ST"
R=$(curl -s -b "$JAR_A" "$BASE/api/submissions/$SID")
check "AC 记录有耗时" '"exec_time_ms"' "$R"
check "AC 记录有内存" '"memory_kb"' "$R"
check "AC 错误信息为空" '"error_message":""' "$R"

echo "== WA 提交 =="
R=$(submit "$JAR_A" cpp "$WA_CODE")
SID_WA=$(echo "$R" | python3 -c "import json,sys;print(json.loads(sys.argv[1])['data']['id'])" "$R" 2>/dev/null)
ST=$(poll_status "$JAR_A" "$SID_WA")
check "WA 提交最终判定 WA" 'WA' "状态=$ST"
R=$(curl -s -b "$JAR_A" "$BASE/api/submissions/$SID_WA")
check "WA 有失败点详情" 'Wrong Answer' "$R"
check "WA 详情含期望输出" 'Expected' "$R"

echo "== CE 提交 =="
R=$(submit "$JAR_A" cpp "$CE_CODE")
SID_CE=$(echo "$R" | python3 -c "import json,sys;print(json.loads(sys.argv[1])['data']['id'])" "$R" 2>/dev/null)
ST=$(poll_status "$JAR_A" "$SID_CE")
check "CE 提交最终判定 COMPILE_ERROR" 'COMPILE_ERROR' "状态=$ST"
R=$(curl -s -b "$JAR_A" "$BASE/api/submissions/$SID_CE")
check "CE 详情含编译输出" 'Compile Error' "$R"

echo "== 参数校验 =="
R=$(submit "$JAR_A" java 'int main(){}')
check "Java 语言 400 PARAM_INVALID" 'PARAM_INVALID' "$R"

R=$(submit "$JAR_A" python 'print(1)')
check "Python 语言 400 PARAM_INVALID" 'PARAM_INVALID' "$R"

R=$(submit "$JAR_A" cpp '')
check "空代码 400 PARAM_INVALID" 'PARAM_INVALID' "$R"

# 超长代码（>100KB）：经临时文件构造请求体，避免 argv 长度限制
CODEFILE="$(mktemp)"
BIGBODY="$(mktemp)"
python3 -c "print('int x;' * 30000)" > "$CODEFILE"
python3 -c "import json,sys;print(json.dumps({'problem_id':int(sys.argv[1]),'language':'cpp','code':open(sys.argv[2]).read()}))" "$PID" "$CODEFILE" > "$BIGBODY"
R=$(curl -s -b "$JAR_A" -X POST "$BASE/api/submissions" \
    -H 'Content-Type: application/json' --data-binary @"$BIGBODY")
check "超长代码 400 PARAM_INVALID" 'PARAM_INVALID' "$R"
rm -f "$CODEFILE" "$BIGBODY"

R=$(curl -s -b "$JAR_S" -w "|%{http_code}" -X POST "$BASE/api/submissions" \
    -H 'Content-Type: application/json' \
    -d "{\"problem_id\":$PID,\"language\":\"cpp\",\"code\":\"int main(){}\"}")
check_code "未入班学生可提交全局题 HTTP 200" 200 "$R"
check "未入班学生可提交全局题" '"ok":true' "$R"

R=$(curl -s -o /dev/null -w "|%{http_code}" -X POST "$BASE/api/submissions" \
    -H 'Content-Type: application/json' \
    -d "{\"problem_id\":$PID,\"language\":\"cpp\",\"code\":\"int main(){}\"}")
check_code "未登录提交 401" 401 "$R"

echo "== GET /api/submissions 历史列表 =="
R=$(curl -s -b "$JAR_A" "$BASE/api/submissions")
check "列表含 AC 提交" "\"id\":$SID" "$R"
check "列表含 WA 提交" "\"id\":$SID_WA" "$R"
check "列表含 problem_title" '"problem_title"' "$R"

echo "== 权限：学生不可见他人提交 =="
R=$(curl -s -b "$JAR_S" -w "|%{http_code}" "$BASE/api/submissions/$SID")
check_code "学生查他人提交 404" 404 "$R"
check "错误码 SUBMISSION_NOT_FOUND" 'SUBMISSION_NOT_FOUND' "$R"

echo "== 重判（POST /api/admin/submissions/:id/rejudge） =="
R=$(curl -s -b "$JAR_A" -X POST "$BASE/api/admin/submissions/$SID_WA/rejudge")
check "重判入队 ok" '"ok":true' "$R"
ST=$(poll_status "$JAR_A" "$SID_WA")
check "重判后再次得到 WA（代码仍错）" 'WA' "状态=$ST"

echo "== 清理 =="
mysql -uoj -poj_password oj_vibecoding -e \
    "DELETE FROM submissions WHERE problem_id=$PID;
     DELETE FROM problems WHERE id=$PID;
     DELETE FROM users WHERE username='$STU';" 2>/dev/null
rm -f "$JAR_A" "$JAR_S"

echo ""
echo "----"
echo "通过 $PASS，失败 $FAIL"
[[ $FAIL -eq 0 ]]
