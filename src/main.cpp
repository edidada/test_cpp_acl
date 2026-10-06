#include <iostream>
#include <cassert>
#include <cstring>
#include <lib_acl.h>

int main() {
    // 初始化 ACL 库
    acl_lib_init();

    std::cout << "ACL version: " << acl_version() << std::endl;

    // ---- 测试 ACL_VSTRING (动态字符串) ----
    ACL_VSTRING *vs = acl_vstring_alloc(64);

    acl_vstring_strcpy(vs, "Hello");
    assert(ACL_VSTRING_LEN(vs) == 5);

    acl_vstring_strcat(vs, ", ACL!");
    assert(ACL_VSTRING_LEN(vs) == 11);
    assert(strcmp(acl_vstring_str(vs), "Hello, ACL!") == 0);

    std::cout << "vstring: " << acl_vstring_str(vs) << std::endl;
    std::cout << "length:  " << ACL_VSTRING_LEN(vs) << std::endl;

    acl_vstring_free(vs);

    // ---- 测试字符串工具函数 ----
    char buf[64];
    // acl_strtrim 移除所有空白字符 (含中间空格)
    strcpy(buf, "  Hello World  ");
    acl_strtrim(buf);
    assert(strcmp(buf, "HelloWorld") == 0);
    std::cout << "trimmed: " << buf << std::endl;

    strcpy(buf, "Hello");
    acl_lowercase(buf);
    assert(strcmp(buf, "hello") == 0);
    std::cout << "lower:   " << buf << std::endl;

    strcpy(buf, "Hello");
    acl_uppercase(buf);
    assert(strcmp(buf, "HELLO") == 0);
    std::cout << "upper:   " << buf << std::endl;

    // 清理 ACL 库
    acl_lib_end();

    std::cout << "All ACL tests passed!" << std::endl;
    return 0;
}
