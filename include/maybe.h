#pragma once

// 轻量 Maybe<T>：语义等价 std::optional<T>
// - 工具链完整支持 C++17 时，直接用 std::optional
// - 否则（如本机 g++ 6.3）退回自实现的最小版本，保证接口不变
// 这样接口代码可以一直写成 C++17 风格，比赛环境升级工具链后无需改动。

#if __cplusplus >= 201703L
#include <optional>
#endif

#ifdef __cpp_lib_optional

template <typename T>
using Maybe = std::optional<T>;

#else

template <typename T>
class Maybe {
public:
    Maybe() : has_(false), val_() {}
    Maybe(const T& v) : has_(true), val_(v) {}

    explicit operator bool() const { return has_; }
    bool has_value() const { return has_; }

    const T& value() const { return val_; }
    T& value() { return val_; }

    const T& operator*() const { return val_; }
    T& operator*() { return val_; }

private:
    bool has_;
    T val_;
};

#endif  // __cpp_lib_optional
