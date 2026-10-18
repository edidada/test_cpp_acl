// xmake smoke demo: minimal ACL (github.com/acl-dev/acl) usage via the
// official xmake-repo package "acl-dev".
//
// The acl-dev package installs headers under include/acl-lib/, so the
// canonical include paths are <acl/lib_acl.h> (C API) and
// <acl_cpp/lib_acl.hpp> (C++ API). __has_include shims keep this file
// working across alternative header layouts.

#if defined(__has_include)
#  if __has_include(<acl/lib_acl.h>)
#    include <acl/lib_acl.h>
#    define ACL_C_HEADER_FOUND 1
#  elif __has_include(<lib_acl.h>)
#    include <lib_acl.h>
#    define ACL_C_HEADER_FOUND 1
#  elif __has_include(<acl-lib/acl/lib_acl.h>)
#    include <acl-lib/acl/lib_acl.h>
#    define ACL_C_HEADER_FOUND 1
#  elif __has_include(<acl-lib/lib_acl.h>)
#    include <acl-lib/lib_acl.h>
#    define ACL_C_HEADER_FOUND 1
#  endif
#endif

#if defined(__has_include)
#  if __has_include(<acl_cpp/lib_acl.hpp>)
#    include <acl_cpp/lib_acl.hpp>
#    define ACL_CPP_HEADER_FOUND 1
#  elif __has_include(<acl/cpp/lib_acl.hpp>)
#    include <acl/cpp/lib_acl.hpp>
#    define ACL_CPP_HEADER_FOUND 1
#  elif __has_include(<acl-lib/acl_cpp/lib_acl.hpp>)
#    include <acl-lib/acl_cpp/lib_acl.hpp>
#    define ACL_CPP_HEADER_FOUND 1
#  endif
#endif

#include <cstdio>

// Fallback declarations so the demo still links against the installed ACL
// static library even when the header layout is unknown.
#if !defined(ACL_C_HEADER_FOUND)
extern "C" {
unsigned int acl_version(void);
const char *acl_version_string(void);
}
#endif

int main(void)
{
	unsigned int numeric = acl_version();
	const char *version = acl_version_string();
	if (version == nullptr) {
		version = "(unknown)";
	}
	std::printf("xmake_smoke: linked ACL version %s (numeric: %u)\n",
		version, numeric);

#if defined(ACL_CPP_HEADER_FOUND)
	{
		acl::string str("xmake_smoke: acl::string works");
		str += " | appended via C++ API";
		std::printf("%s (len=%zu)\n", str.c_str(), str.size());
	}
#endif

	std::printf("xmake_smoke: OK\n");
	return 0;
}
