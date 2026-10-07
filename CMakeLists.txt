cmake_minimum_required(VERSION 3.20)
project(test_cpp_acl C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# 从 GitHub 拉取 ACL 库 (国内可 cmake -DACL_GIT_REPOSITORY=https://gitee.com/acl-dev/acl.git 走镜像)
if(NOT DEFINED ACL_GIT_REPOSITORY)
    set(ACL_GIT_REPOSITORY https://github.com/acl-dev/acl.git)
endif()

FetchContent_Declare(
    acl
    GIT_REPOSITORY ${ACL_GIT_REPOSITORY}
    GIT_TAG        master
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(acl)

# ---- 平台相关系统库 ----
if(WIN32)
    set(PLATFORM_LIBS ws2_32)
else()
    set(PLATFORM_LIBS pthread dl)
endif()

# ---- 头文件路径 ----
set(ACL_INCLUDE_DIRS
    ${acl_SOURCE_DIR}/lib_acl/include
    ${acl_SOURCE_DIR}/lib_protocol/include
    ${acl_SOURCE_DIR}/lib_acl_cpp/include
    ${acl_SOURCE_DIR}/lib_acl_cpp/include/acl_cpp
)

# ---- 链接库层级 ----
# C 核心: 仅 acl_static
set(ACL_LIBS_C          acl_static ${PLATFORM_LIBS})
# 协议库: protocol_static + acl_static
set(ACL_LIBS_PROTOCOL   protocol_static acl_static ${PLATFORM_LIBS})
# C++ 封装: acl_cpp_static + protocol_static + acl_static
set(ACL_LIBS_CPP        acl_cpp_static protocol_static acl_static ${PLATFORM_LIBS})

# ---- 辅助函数：添加测试可执行文件 ----
# add_acl_test(name source tier)
#   tier = C | PROTOCOL | CPP
function(add_acl_test name source tier)
    add_executable(${name} ${source})
    target_include_directories(${name} PRIVATE ${ACL_INCLUDE_DIRS})
    if(tier STREQUAL "C")
        target_link_libraries(${name} PRIVATE ${ACL_LIBS_C})
    elseif(tier STREQUAL "PROTOCOL")
        target_link_libraries(${name} PRIVATE ${ACL_LIBS_PROTOCOL})
    elseif(tier STREQUAL "CPP")
        target_link_libraries(${name} PRIVATE ${ACL_LIBS_CPP})
    endif()
    # MSVC 源码使用 UTF-8
    if(MSVC)
        target_compile_options(${name} PRIVATE /utf-8 /wd4819 /wd4996)
        target_compile_definitions(${name} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endfunction()

# ---- 原有冒烟测试 ----
add_acl_test(test_cpp_acl src/main.cpp CPP)

# ---- C 核心库测试 (lib_acl) ----
add_acl_test(test_c_memory      src/c/test_c_memory.cpp      C)
add_acl_test(test_c_string      src/c/test_c_string.cpp      C)
add_acl_test(test_c_containers  src/c/test_c_containers.cpp  C)
add_acl_test(test_c_network     src/c/test_c_network.cpp     C)
add_acl_test(test_c_event       src/c/test_c_event.cpp       C)
add_acl_test(test_c_thread      src/c/test_c_thread.cpp      C)
add_acl_test(test_c_json_xml    src/c/test_c_json_xml.cpp    C)
add_acl_test(test_c_misc        src/c/test_c_misc.cpp        C)

# ---- 协议库测试 (lib_protocol) ----
add_acl_test(test_protocol_http src/protocol/test_protocol_http.cpp PROTOCOL)
add_acl_test(test_protocol_misc src/protocol/test_protocol_misc.cpp PROTOCOL)

# ---- C++ 封装库测试 (lib_acl_cpp) ----
add_acl_test(test_cpp_string_json src/cpp/test_cpp_string_json.cpp CPP)
add_acl_test(test_cpp_stream      src/cpp/test_cpp_stream.cpp      CPP)
add_acl_test(test_cpp_thread      src/cpp/test_cpp_thread.cpp      CPP)
add_acl_test(test_cpp_log         src/cpp/test_cpp_log.cpp         CPP)
add_acl_test(test_cpp_misc        src/cpp/test_cpp_misc.cpp        CPP)
add_acl_test(test_cpp_http        src/cpp/test_cpp_http.cpp        CPP)
add_acl_test(test_cpp_redis       src/cpp/test_cpp_redis.cpp       CPP)
add_acl_test(test_cpp_db          src/cpp/test_cpp_db.cpp          CPP)
add_acl_test(test_cpp_master      src/cpp/test_cpp_master.cpp      CPP)
add_acl_test(test_cpp_connpool    src/cpp/test_cpp_connpool.cpp    CPP)
add_acl_test(test_cpp_modules     src/cpp/test_cpp_modules.cpp     CPP)

# ---- CTest 集成 ----
enable_testing()
file(GLOB_RECURSE ALL_TEST_TARGETS "")
foreach(tgt
    test_cpp_acl
    test_c_memory test_c_string test_c_containers test_c_network
    test_c_event test_c_thread test_c_json_xml test_c_misc
    test_protocol_http test_protocol_misc
    test_cpp_string_json test_cpp_stream test_cpp_thread test_cpp_log
    test_cpp_misc test_cpp_http test_cpp_redis test_cpp_db
    test_cpp_master test_cpp_connpool test_cpp_modules
)
    add_test(NAME ${tgt} COMMAND ${tgt})
endforeach()
