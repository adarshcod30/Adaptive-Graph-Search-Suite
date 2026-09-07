// Minimal test harness. Deliberately dependency-free: the project ships with
// no third-party requirements, and a full framework would be more code than
// the suite it runs.
#pragma once

#include <cmath>
#include <cstdio>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace mt {

struct Case {
    std::string suite;
    std::string name;
    std::function<void()> fn;
};

inline std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

struct Failure {
    std::string message;
};

inline int& failure_count() {
    static int n = 0;
    return n;
}
inline int& check_count() {
    static int n = 0;
    return n;
}

inline void fail(const std::string& file, int line, const std::string& expr,
                 const std::string& detail) {
    std::ostringstream os;
    os << file << ":" << line << "  " << expr;
    if (!detail.empty()) os << "\n      " << detail;
    throw Failure{os.str()};
}

inline bool close(double a, double b, double tol) {
    if (std::isinf(a) || std::isinf(b)) return a == b;
    return std::abs(a - b) <= tol * std::max(1.0, std::max(std::abs(a), std::abs(b)));
}

inline int run(const std::string& filter) {
    std::string last_suite;
    int passed = 0, failed = 0;
    for (auto& c : registry()) {
        if (!filter.empty() && c.suite.find(filter) == std::string::npos &&
            c.name.find(filter) == std::string::npos) {
            continue;
        }
        if (c.suite != last_suite) {
            std::cout << "\n\033[1m" << c.suite << "\033[0m\n";
            last_suite = c.suite;
        }
        try {
            c.fn();
            std::cout << "  \033[32mPASS\033[0m  " << c.name << "\n";
            ++passed;
        } catch (const Failure& f) {
            std::cout << "  \033[31mFAIL\033[0m  " << c.name << "\n      " << f.message << "\n";
            ++failed;
        } catch (const std::exception& e) {
            std::cout << "  \033[31mFAIL\033[0m  " << c.name << "\n      threw: " << e.what()
                      << "\n";
            ++failed;
        }
    }
    std::cout << "\n" << (failed == 0 ? "\033[32m" : "\033[31m") << passed << " passed, " << failed
              << " failed\033[0m  (" << check_count() << " assertions)\n";
    return failed == 0 ? 0 : 1;
}

struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> fn) {
        registry().push_back({suite, name, std::move(fn)});
    }
};

}  // namespace mt

#define MT_CAT_(a, b) a##b
#define MT_CAT(a, b) MT_CAT_(a, b)

#define TEST(suite_name, case_name)                                                   \
    static void MT_CAT(mt_case_, __LINE__)();                                         \
    static ::mt::Registrar MT_CAT(mt_reg_, __LINE__)(suite_name, case_name,           \
                                                     MT_CAT(mt_case_, __LINE__));     \
    static void MT_CAT(mt_case_, __LINE__)()

#define CHECK(expr)                                                                   \
    do {                                                                              \
        ++::mt::check_count();                                                        \
        if (!(expr)) ::mt::fail(__FILE__, __LINE__, "CHECK(" #expr ")", "");          \
    } while (0)

#define CHECK_MSG(expr, detail)                                                       \
    do {                                                                              \
        ++::mt::check_count();                                                        \
        if (!(expr)) {                                                                \
            std::ostringstream mt_os_;                                                \
            mt_os_ << detail;                                                         \
            ::mt::fail(__FILE__, __LINE__, "CHECK(" #expr ")", mt_os_.str());         \
        }                                                                             \
    } while (0)

#define CHECK_EQ(a, b)                                                                \
    do {                                                                              \
        ++::mt::check_count();                                                        \
        const auto mt_a_ = (a);                                                       \
        const auto mt_b_ = (b);                                                       \
        if (!(mt_a_ == mt_b_)) {                                                      \
            std::ostringstream mt_os_;                                                \
            mt_os_ << "got " << mt_a_ << ", expected " << mt_b_;                      \
            ::mt::fail(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b ")", mt_os_.str()); \
        }                                                                             \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                         \
    do {                                                                              \
        ++::mt::check_count();                                                        \
        const double mt_a_ = (a);                                                     \
        const double mt_b_ = (b);                                                     \
        if (!::mt::close(mt_a_, mt_b_, tol)) {                                        \
            std::ostringstream mt_os_;                                                \
            mt_os_ << "got " << mt_a_ << ", expected " << mt_b_;                      \
            ::mt::fail(__FILE__, __LINE__, "CHECK_NEAR(" #a ", " #b ")", mt_os_.str()); \
        }                                                                             \
    } while (0)
