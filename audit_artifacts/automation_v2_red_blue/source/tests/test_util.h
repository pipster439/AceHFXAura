#pragma once

#include <iostream>
#include <string>

// 真实失败计数器宏与断言定义。
// 在 Release + /O2 + NDEBUG 构建下，普通 assert() 会被预处理器彻底消除，导致测试丧失真实校验力。
// CHECK 宏保证校验力在任何构建配置下均真实生效：
// - 判定为 true 时输出 [PASS] 提示信息到 stdout
// - 判定为 false 时输出 [FAIL] 提示信息、位置、表达式到 stdout（确保自动化测试在 stdout 准确统计 [FAIL] 次数）与 stderr，并使 failures 局部计数器自增。
#define CHECK(cond, msg) \
    do { \
        if (cond) { \
            std::cout << "  [PASS] " << (msg) << "\n"; \
        } else { \
            ++failures; \
            std::cout << "  [FAIL] " << (msg) << "\n" \
                      << "         位置: " << __FILE__ << ":" << __LINE__ << "\n" \
                      << "         表达式: " << #cond << "\n"; \
            std::cerr << "  [FAIL] " << (msg) << " (" << __FILE__ << ":" << __LINE__ << ")\n"; \
        } \
    } while (0)
