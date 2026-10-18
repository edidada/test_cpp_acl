-- Minimal xmake demo for the ACL network library, using the official
-- xmake-repo package "acl-dev" (acl compiled from source, static libs).
-- The full CMake suite on this repo is intentionally NOT ported here.

set_project("test_cpp_acl-xmake")
set_version("0.1.0")
set_xmakever("2.7.9")

add_rules("mode.debug", "mode.release")
set_warnings("all")

target("xmake_smoke")
    set_kind("binary")
    set_languages("c++17")
    add_files("demo_xmake/*.cpp")
    add_packages("acl-dev")

    if is_os("windows") then
        -- The acl-dev CMake build defaults to the dynamic MSVC runtime.
        set_runtimes("MD")
        -- The recipe declares add_links only for non-Windows platforms;
        -- on Windows the static import libs are named after the CMake
        -- OUTPUT_NAMEs: acl_cpp, protocol, fiber_cpp, fiber, acl.
        add_links("acl_cpp", "protocol", "fiber_cpp", "fiber", "acl")
        add_syslinks("ws2_32", "iphlpapi", "kernel32", "user32", "gdi32")
        if is_msvc() then
            add_defines("_CRT_SECURE_NO_WARNINGS")
            add_cxflags("/utf-8", "/wd4819", "/wd4996", {force = true})
        end
    end
    -- NOTE: the acl-dev package already contributes pthread/dl on Linux
    -- and nothing extra on macOS (no libdl there), so no target-level
    -- syslinks are needed outside of Windows.
