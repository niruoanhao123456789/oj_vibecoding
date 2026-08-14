# tests/unit/CMakeLists.txt
# gtest 单元测试。
#
# DB 相关用例（oj_db_test）为集成测试，需要本地 MySQL 已按
# sql/init_db.sql 与 sql/schema.sql 完成初始化；连接失败或 schema 缺失时
# 相关用例会 SKIP（而非失败）。server 冒烟测试同理。
#
# 运行方式：
#   cmake --build build
#   ctest --test-dir build --output-on-failure
#   或直接运行 ./build/tests/unit/oj_*_test

find_package(GTest REQUIRED)
find_package(Threads REQUIRED)

set(OJ_TEST_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)

set(OJ_TEST_CORE
  ${OJ_TEST_SRC_DIR}/src/db.cpp
  ${OJ_TEST_SRC_DIR}/src/config.cpp
  ${OJ_TEST_SRC_DIR}/src/log.cpp
)

# ---- 公共工具：注册一个 gtest 可执行目标 ----
function(oj_register_gtest name)
  add_executable(${name} ${ARGN})
  target_include_directories(${name} PRIVATE
    ${OJ_TEST_SRC_DIR}/src
    /usr/include/jsoncpp
  )
  target_compile_definitions(${name} PRIVATE
    OJ_SOURCE_DIR="${OJ_TEST_SRC_DIR}"
  )
  target_link_libraries(${name} PRIVATE
    GTest::gtest
    GTest::gtest_main
    ${MYSQL_CLIENT_LIB}
    jsoncpp
    Threads::Threads
  )
  add_test(NAME ${name} COMMAND ${name})
endfunction()

# oj_field_test：Field 类型转换纯单元测试
oj_register_gtest(oj_field_test field_test.cpp ${OJ_TEST_CORE})

# oj_config_test：配置加载纯单元测试
oj_register_gtest(oj_config_test config_test.cpp ${OJ_TEST_CORE})

# oj_log_test：日志模块纯单元测试
oj_register_gtest(oj_log_test log_test.cpp ${OJ_TEST_CORE})

# oj_problem_test：题目 JSON 解析纯单元测试 + 导入集成测试
oj_register_gtest(oj_problem_test problem_test.cpp ${OJ_TEST_CORE}
  ${OJ_TEST_SRC_DIR}/src/problem.cpp
)

# oj_db_test：DB 访问层集成测试（连接/参数化执行/类型化查询/schema）
oj_register_gtest(oj_db_test db_test.cpp ${OJ_TEST_CORE})

# oj_server_test：HTTP 服务冒烟测试（health/静态托管/404）
add_executable(oj_server_test server_test.cpp
  ${OJ_TEST_SRC_DIR}/src/server.cpp
  ${OJ_TEST_CORE}
)
target_include_directories(oj_server_test PRIVATE
  ${OJ_TEST_SRC_DIR}/src
  ${HTTPLIB_INCLUDE_DIR}
  /usr/include/jsoncpp
)
target_compile_definitions(oj_server_test PRIVATE
  HTTPLIB_HEADER_ONLY
  OJ_SOURCE_DIR="${OJ_TEST_SRC_DIR}"
)
target_link_libraries(oj_server_test PRIVATE
  GTest::gtest
  GTest::gtest_main
  ${MYSQL_CLIENT_LIB}
  cpp-httplib
  jsoncpp
  Threads::Threads
)
add_test(NAME oj_server_test COMMAND oj_server_test)
