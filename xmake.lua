-- xmake demo build for the ACL network library smoke test.
-- Requires the official xmake-repo package "acl-dev".
set_project("test_cpp_acl_xmake")
set_version("0.1.0")

add_rules("mode.debug", "mode.release")

set_languages("c11", "cxx17")
set_warnings("all")

-- Official xmake-repo package for github.com/acl-dev/acl
add_requires("acl-dev")

target("xmake_smoke")
    set_kind("binary")
    add_files("demo_xmake/xmake_smoke.cpp")
    add_packages("acl-dev")
    if is_plat("windows") then
        add_syslinks("ws2_32", "iphlpapi", "kernel32", "user32", "gdi32")
    elseif is_plat("macosx", "linux", "bsd") then
        add_syslinks("pthread", "dl")
    end
