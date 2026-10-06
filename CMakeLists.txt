cmake_minimum_required(VERSION 3.20)
project(test_cpp_acl)

set(CMAKE_CXX_STANDARD 17)

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

add_executable(test_cpp_acl src/main.cpp)

# ACL 的 CMake target 名为 acl_static / protocol_static / acl_cpp_static
target_link_libraries(test_cpp_acl PRIVATE acl_cpp_static protocol_static acl_static)

# Windows 需要额外链接 Winsock
if(WIN32)
    target_link_libraries(test_cpp_acl PRIVATE ws2_32)
elseif(UNIX)
    target_link_libraries(test_cpp_acl PRIVATE pthread dl)
endif()

# 头文件路径
target_include_directories(test_cpp_acl PRIVATE
    ${acl_SOURCE_DIR}/lib_acl/include
    ${acl_SOURCE_DIR}/lib_protocol/include
    ${acl_SOURCE_DIR}/lib_acl_cpp/include
)
