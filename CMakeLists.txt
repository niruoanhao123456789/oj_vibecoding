cmake_minimum_required(VERSION 3.16)

project(oj_vibecoding CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2")
add_compile_options(-Wall -Wextra)

find_package(Threads REQUIRED)

find_path(HTTPLIB_INCLUDE_DIR httplib.h REQUIRED)
find_library(MYSQL_CLIENT_LIB NAMES mysqlclient REQUIRED)

add_executable(oj_server
  src/main.cpp
  src/server.cpp
  src/auth.cpp
  src/config.cpp
  src/db.cpp
  src/log.cpp
  src/problem.cpp
  src/ojclass.cpp
)

target_include_directories(oj_server PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${HTTPLIB_INCLUDE_DIR}
  /usr/include/jsoncpp
)

target_compile_definitions(oj_server PRIVATE HTTPLIB_HEADER_ONLY)

target_link_libraries(oj_server PRIVATE
  ${MYSQL_CLIENT_LIB}
  cpp-httplib
  jsoncpp
  Threads::Threads
)

install(TARGETS oj_server RUNTIME DESTINATION bin)

# 命令行题目导入工具（阶段 2）
add_executable(oj_import
  tools/oj_import.cpp
  src/problem.cpp
  src/config.cpp
  src/db.cpp
  src/log.cpp
)
target_include_directories(oj_import PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  /usr/include/jsoncpp
)
target_link_libraries(oj_import PRIVATE
  ${MYSQL_CLIENT_LIB}
  jsoncpp
  Threads::Threads
)
install(TARGETS oj_import RUNTIME DESTINATION bin)

include(CTest)

if(BUILD_TESTING)
  add_subdirectory(tests/unit)
endif()
