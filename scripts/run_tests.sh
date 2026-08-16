#!/usr/bin/env bash
# scripts/run_tests.sh — 阶段 9 自动化测试入口。
#
# 依次执行：
#   1) CMake 构建（单元测试可执行文件 + oj_server / oj_import）
#   2) 单元测试（ctest；DB 相关用例需本地 MySQL，缺失自动 SKIP）
#   3) 接口测试（tests/api/*.sh）+ 端到端冒烟（tests/e2e/run_smoke.sh）：
#      - 已设置 OJ_TEST_BASE        → 直接对指定地址测试（外部服务）
#      - 本地 8080 端口已有 oj_server → 直接测试
#      - 否则在 18082 端口临时启动实例（复用本地 MySQL，日志写入 /tmp），
#        测完自动关闭
#
# 用法：
#   ./scripts/run_tests.sh [--skip-server] [--no-unit]
#     --skip-server  跳过接口/端到端测试，仅跑构建 + 单元测试
#     --no-unit      跳过构建与单元测试，仅跑接口/端到端测试
#
#   环境变量：
#     OJ_TEST_BASE=http://host:port  使用外部运行中的服务，不自动起停
set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"

SKIP_SERVER=0
NO_UNIT=0
for a in "$@"; do
    case "$a" in
        --skip-server) SKIP_SERVER=1 ;;
        --no-unit) NO_UNIT=1 ;;
        *) echo "未知参数: $a" >&2; exit 2 ;;
    esac
done

FAIL=0
step() { echo; echo "==================== $* ===================="; }
summary() {
    echo
    if [[ $FAIL -eq 0 ]]; then
        echo "全部测试通过 ✔"
    else
        echo "存在失败用例（FAIL=$FAIL） ✘"
    fi
}

# ---------- 1. 构建 ----------
if [[ $NO_UNIT -eq 0 ]]; then
    step "1/3 构建"
    cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Release || { echo "cmake 配置失败"; exit 1; }
    cmake --build "${BUILD_DIR}" -j"$(nproc 2>/dev/null || echo 2)" || { echo "编译失败"; exit 1; }

    # ---------- 2. 单元测试 ----------
    step "2/3 单元测试（ctest）"
    ctest --test-dir "${BUILD_DIR}" --output-on-failure || FAIL=$((FAIL + 1))
else
    step "跳过构建与单元测试（--no-unit）"
fi

# ---------- 3. 接口 + 端到端 ----------
API_TESTS=(
    "${ROOT_DIR}/tests/api/test_auth.sh"
    "${ROOT_DIR}/tests/api/test_problems.sh"
    "${ROOT_DIR}/tests/api/test_submissions.sh"
    "${ROOT_DIR}/tests/api/test_run.sh"
    "${ROOT_DIR}/tests/api/test_admin_problem.sh"
    "${ROOT_DIR}/tests/api/test_admin_users.sh"
    "${ROOT_DIR}/tests/api/test_class.sh"
    "${ROOT_DIR}/tests/e2e/run_smoke.sh"
)

run_api_tests() {
    local base="$1"
    for t in "${API_TESTS[@]}"; do
        step "$(basename "$t") @ ${base}"
        if bash "$t" "$base"; then
            echo "✔ $(basename "$t") 通过"
        else
            echo "✘ $(basename "$t") 失败"
            FAIL=$((FAIL + 1))
        fi
    done
}

if [[ $SKIP_SERVER -eq 1 ]]; then
    step "跳过接口/端到端测试（--skip-server）"
elif [[ -n "${OJ_TEST_BASE:-}" ]]; then
    step "3/3 接口 + 端到端 @ ${OJ_TEST_BASE}（外部服务）"
    run_api_tests "${OJ_TEST_BASE}"
else
    # 检查 8080 是否已有服务
    if curl -s --max-time 2 "http://127.0.0.1:8080/api/health" >/dev/null 2>&1; then
        step "3/3 接口 + 端到端 @ http://127.0.0.1:8080（检测到本地服务）"
        run_api_tests "http://127.0.0.1:8080"
    else
        step "3/3 接口 + 端到端（临时启动实例 @ :18082）"
        if [[ ! -x "${BUILD_DIR}/oj_server" ]]; then
            echo "oj_server 不存在，请先构建（./scripts/run_tests.sh --no-unit 需已有构建）"
            exit 1
        fi
        TEST_PORT=18082
        TMP_CFG="$(mktemp)"
        python3 -c "
import json
cfg = json.load(open('${ROOT_DIR}/config/server.json'))
cfg['port'] = $TEST_PORT
cfg['host'] = '127.0.0.1'
cfg['data_dir'] = '${ROOT_DIR}/data'
cfg['submission_dir'] = '${ROOT_DIR}/data/submissions'
cfg['frontend_dir'] = '${ROOT_DIR}/frontend'
cfg['log_dir'] = '/tmp/oj_test_logs'
cfg['worker_num'] = 2
json.dump(cfg, open('$TMP_CFG', 'w'), indent=2)
"
        mkdir -p /tmp/oj_test_logs
        (cd "${ROOT_DIR}" && ./build/oj_server "$TMP_CFG" &>/tmp/oj_test_server.log & echo $! > /tmp/oj_test_server.pid)
        SERVER_PID="$(cat /tmp/oj_test_server.pid 2>/dev/null)"
        trap '[[ -n "$SERVER_PID" ]] && kill "$SERVER_PID" 2>/dev/null; rm -f "$TMP_CFG" /tmp/oj_test_server.pid' EXIT

        # 等待就绪
        READY=0
        for _ in $(seq 1 100); do
            if curl -s --max-time 1 "http://127.0.0.1:$TEST_PORT/api/health" >/dev/null 2>&1; then
                READY=1
                break
            fi
            sleep 0.1
        done
        if [[ $READY -eq 0 ]]; then
            echo "临时服务未能启动，日志："
            cat /tmp/oj_test_server.log
            exit 1
        fi
        run_api_tests "http://127.0.0.1:$TEST_PORT"
    fi
fi

summary
[[ $FAIL -eq 0 ]]
