// xmake_smoke.cpp -- minimal smoke demo built against the xmake-repo
// "acl-dev" package (https://github.com/acl-dev/acl).
//
// The acl-dev package installs headers under <prefix>/include/acl-lib/
// with per-library subdirs (acl/, acl_cpp/, protocol/, fiber/, ...), and
// exposes "include" and "include/acl-lib" as includedirs. Older/alternative
// layouts may expose the umbrella headers directly, so every include below
// is probed with __has_include to keep this one file portable.

#include <cstdio>
#include <cstring>

// ---- ACL C umbrella header (declares acl_version(), acl_lib_init(), ...) ----
#if defined(__has_include)
#  if __has_include(<acl/lib_acl.h>)
#    include <acl/lib_acl.h>
#    define XMAKE_ACL_C_OK 1
#    define XMAKE_ACL_C_HEADER "acl/lib_acl.h"
#  elif __has_include(<lib_acl.h>)
#    include <lib_acl.h>
#    define XMAKE_ACL_C_OK 1
#    define XMAKE_ACL_C_HEADER "lib_acl.h"
#  elif __has_include(<acl-lib/acl/lib_acl.h>)
#    include <acl-lib/acl/lib_acl.h>
#    define XMAKE_ACL_C_OK 1
#    define XMAKE_ACL_C_HEADER "acl-lib/acl/lib_acl.h"
#  endif
#endif

// ---- ACL C++ umbrella header (declares class acl::string, ...) ----
#if defined(__has_include)
#  if __has_include(<acl_cpp/lib_acl.hpp>)
#    include <acl_cpp/lib_acl.hpp>
#    define XMAKE_ACL_CPP_OK 1
#    define XMAKE_ACL_CPP_HEADER "acl_cpp/lib_acl.hpp"
#  elif __has_include(<cpp/lib_acl_cpp.h>)
#    include <cpp/lib_acl_cpp.h>
#    define XMAKE_ACL_CPP_OK 1
#    define XMAKE_ACL_CPP_HEADER "cpp/lib_acl_cpp.h"
#  elif __has_include(<acl/cpp/acl.h>)
#    include <acl/cpp/acl.h>
#    define XMAKE_ACL_CPP_OK 1
#    define XMAKE_ACL_CPP_HEADER "acl/cpp/acl.h"
#  endif
#endif

#ifndef XMAKE_ACL_C_OK
#  error "xmake_smoke: cannot locate the ACL C umbrella header (lib_acl.h) via the acl-dev package"
#endif

int main(void)
{
	std::printf("[xmake_smoke] ACL C header  : %s\n", XMAKE_ACL_C_HEADER);
#ifdef XMAKE_ACL_CPP_HEADER
	std::printf("[xmake_smoke] ACL C++ header: %s\n", XMAKE_ACL_CPP_HEADER);
#endif

	// Trivial ACL C API #1: library version string.
	const char *version = acl_version();
	if (version == NULL || version[0] == '\0')
	{
		std::fprintf(stderr, "[xmake_smoke] acl_version() returned empty\n");
		return 1;
	}
	std::printf("[xmake_smoke] acl_version() = %s\n", version);

	// Trivial ACL C API #2: init/end round trip of the library.
	acl_lib_init();
	acl_lib_end();
	std::printf("[xmake_smoke] acl_lib_init()/acl_lib_end() ok\n");

#ifdef XMAKE_ACL_CPP_OK
	// Trivial ACL C++ API: acl::string (also proves we link lib_acl_cpp).
	acl::string s("hello-from-xmake");
	s += "-acl-dev";
	if (strcmp(s.c_str(), "hello-from-xmake-acl-dev") != 0)
	{
		std::fprintf(stderr, "[xmake_smoke] acl::string mismatch: %s\n", s.c_str());
		return 1;
	}
	std::printf("[xmake_smoke] acl::string   = %s (len=%u)\n",
		s.c_str(), static_cast<unsigned>(s.length()));
#endif

	std::printf("[xmake_smoke] SUCCESS\n");
	return 0;
}
