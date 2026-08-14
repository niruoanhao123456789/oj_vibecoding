#!/usr/bin/env bash
# scripts/import_problem.sh — 通过 oj_import 命令行工具导入单题。
#
# 用法：
#   ./scripts/import_problem.sh <problem.json> [--config <server.json>]
#
# 示例：
#   ./scripts/import_problem.sh problems/aplusb/problem.json
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
OJ_IMPORT="${ROOT_DIR}/build/oj_import"

if [[ $# -lt 1 ]]; then
    echo "usage: import_problem.sh <problem.json> [--config <server.json>]" >&2
    exit 2
fi

if [[ ! -x "${OJ_IMPORT}" ]]; then
    echo "error: ${OJ_IMPORT} not found, run 'cmake --build build' first" >&2
    exit 1
fi

cd "${ROOT_DIR}"
"${OJ_IMPORT}" "$@"
