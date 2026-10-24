// Edited by: OpenAI-Codex_Responsible: Source_Code
#pragma once

#include <cmath>
#include <cstddef>
#include <exception>
#include <functional>
#include <iostream>
#include <ostream>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace ets2ap::test {

struct TestCase {
    std::string name;
    std::function<void()> run;
};

struct RunResult {
    std::size_t tests = 0;
    std::size_t checks = 0;
    std::size_t failures = 0;
    int exit_code = 1;
};

inline std::vector<TestCase>& Registry() {
    static std::vector<TestCase> cases;
    return cases;
}

inline bool RegisterCase(const char* name, void (*run)()) {
    Registry().push_back({name, run});
    return true;
}

struct Context {
    std::ostream* output;
    std::size_t checks = 0;
    std::size_t failures = 0;
};

inline thread_local Context* active_context = nullptr;

template <typename T>
std::string ValueText(const T& value) {
    std::ostringstream stream;
    if constexpr (std::is_enum_v<T>) {
        stream << static_cast<std::underlying_type_t<T>>(value);
    } else if constexpr (requires { stream << value; }) {
        stream << value;
    } else {
        stream << sizeof(T) << " bytes";
    }
    return stream.str();
}

inline void Report(bool passed, const char* expression, const char* file, int line,
                   const std::string& left = {}, const std::string& right = {}) {
    Context& context = *active_context;
    ++context.checks;
    if (passed) return;
    ++context.failures;
    *context.output << file << ':' << line << ": CHECK failed: " << expression;
    if (!left.empty() || !right.empty()) {
        *context.output << " (left=" << left << ", right=" << right << ')';
    }
    *context.output << '\n';
}

inline void Check(bool actual, const char* expression, const char* file, int line) {
    Report(actual, expression, file, line, actual ? "true" : "false", "true");
}

template <typename A, typename B>
void CompareEqual(const A& a, const B& b, const char* expression, const char* file, int line) {
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        Report(std::cmp_equal(a, b), expression, file, line, ValueText(a), ValueText(b));
    } else {
        Report(a == b, expression, file, line, ValueText(a), ValueText(b));
    }
}

template <typename A, typename B>
void CompareNotEqual(const A& a, const B& b, const char* expression, const char* file, int line) {
    if constexpr (std::is_integral_v<A> && std::is_integral_v<B>) {
        Report(!std::cmp_equal(a, b), expression, file, line, ValueText(a), ValueText(b));
    } else {
        Report(a != b, expression, file, line, ValueText(a), ValueText(b));
    }
}

template <typename A, typename B, typename E>
void CompareNear(const A& a, const B& b, const E& epsilon, const char* expression,
                 const char* file, int line) {
    const double diff = std::fabs(static_cast<double>(a) - static_cast<double>(b));
    const double tolerance = static_cast<double>(epsilon);
    Report(std::isfinite(diff) && std::isfinite(tolerance) && tolerance >= 0 && diff <= tolerance,
           expression, file, line, ValueText(a), ValueText(b));
}

inline RunResult RunCases(const std::vector<TestCase>& cases, std::string_view filter,
                          bool list_only, std::ostream& output) {
    RunResult result;
    for (const auto& test : cases) {
        if (test.name.find(filter) == std::string::npos) continue;
        if (list_only) {
            output << test.name << '\n';
            continue;
        }
        ++result.tests;
        Context context{&output};
        Context* previous = active_context;
        active_context = &context;
        try {
            test.run();
        } catch (const std::exception& ex) {
            ++context.failures;
            output << test.name << ": exception: " << ex.what() << '\n';
        } catch (...) {
            ++context.failures;
            output << test.name << ": unknown exception\n";
        }
        active_context = previous;
        result.checks += context.checks;
        result.failures += context.failures;
        output << test.name << ": " << context.checks << " checks\n";
    }
    if (list_only) {
        result.exit_code = 0;
    } else {
        output << result.tests << " tests, " << result.checks << " checks, "
               << result.failures << " failures\n";
        result.exit_code = result.failures != 0 || result.tests == 0 || result.checks == 0 ? 1 : 0;
    }
    return result;
}

inline int Main(int argc, char** argv) {
    bool list_only = false;
    std::string_view filter;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--list") list_only = true;
        else if (arg.starts_with("--filter=")) filter = arg.substr(9);
        else {
            std::cerr << "Unknown argument: " << arg << '\n';
            return 1;
        }
    }
    return RunCases(Registry(), filter, list_only, std::cout).exit_code;
}

}  // namespace ets2ap::test

#define ETS2AP_TEST(Name) \
    static void Name(); \
    [[maybe_unused]] static const bool Name##_registered = ::ets2ap::test::RegisterCase(#Name, &Name); \
    static void Name()
#define CHECK(expr) ::ets2ap::test::Check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define CHECK_EQ(a, b) ::ets2ap::test::CompareEqual((a), (b), #a " == " #b, __FILE__, __LINE__)
#define CHECK_NE(a, b) ::ets2ap::test::CompareNotEqual((a), (b), #a " != " #b, __FILE__, __LINE__)
#define CHECK_NEAR(a, b, eps) ::ets2ap::test::CompareNear((a), (b), (eps), #a " ~= " #b, __FILE__, __LINE__)
#define ETS2AP_TEST_MAIN() int main(int argc, char** argv) { return ::ets2ap::test::Main(argc, argv); }
