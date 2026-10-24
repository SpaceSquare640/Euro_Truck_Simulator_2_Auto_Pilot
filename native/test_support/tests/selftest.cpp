// Edited by: OpenAI-Codex_Responsible: Source_Code
#include "ets2ap/test/test_framework.h"

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void Passing() { CHECK_EQ(2 + 2, 4); }
void Failing() { CHECK_EQ(2 + 2, 5); }
void FailingBoolean() { CHECK(false); }
void ThrowsStandard() { throw std::runtime_error("expected test exception"); }
void ThrowsUnknown() { throw 7; }
}

ETS2AP_TEST(FailingCheckProducesNonzero) {
    std::ostringstream output;
    const auto result = ets2ap::test::RunCases({{"failure", Failing}}, "", false, output);
    CHECK_EQ(result.tests, 1u);
    CHECK_EQ(result.checks, 1u);
    CHECK_EQ(result.failures, 1u);
    CHECK_NE(result.exit_code, 0);
    CHECK(output.str().find("2 + 2") != std::string::npos);
}

ETS2AP_TEST(StandardAndUnknownExceptionsProduceFailures) {
    std::ostringstream output;
    const auto result = ets2ap::test::RunCases({{"standard", ThrowsStandard}, {"unknown", ThrowsUnknown}}, "", false, output);
    CHECK_EQ(result.tests, 2u);
    CHECK_EQ(result.failures, 2u);
    CHECK_NE(result.exit_code, 0);
    CHECK(output.str().find("expected test exception") != std::string::npos);
}

ETS2AP_TEST(BooleanFailureReportsBothValues) {
    std::ostringstream output;
    const auto result = ets2ap::test::RunCases({{"boolean", FailingBoolean}}, "", false, output);
    CHECK_EQ(result.failures, 1u);
    CHECK(output.str().find("left=false, right=true") != std::string::npos);
}

ETS2AP_TEST(NoSelectedTestsProducesNonzero) {
    std::ostringstream output;
    const auto result = ets2ap::test::RunCases({}, "", false, output);
    CHECK_EQ(result.tests, 0u);
    CHECK_NE(result.exit_code, 0);
}

ETS2AP_TEST(ListAndFilterSelectTests) {
    const std::vector<ets2ap::test::TestCase> cases{{"alpha", Passing}, {"beta", Failing}};
    std::ostringstream list_output;
    const auto listed = ets2ap::test::RunCases(cases, "", true, list_output);
    CHECK_EQ(listed.tests, 0u);
    CHECK_EQ(listed.exit_code, 0);
    CHECK(list_output.str().find("alpha") != std::string::npos);
    CHECK(list_output.str().find("beta") != std::string::npos);
    std::ostringstream filtered_output;
    const auto filtered = ets2ap::test::RunCases(cases, "alpha", false, filtered_output);
    CHECK_EQ(filtered.tests, 1u);
    CHECK_EQ(filtered.checks, 1u);
    CHECK_EQ(filtered.failures, 0u);
    CHECK_EQ(filtered.exit_code, 0);
}

ETS2AP_TEST(ZeroChecksProducesNonzero) {
    std::ostringstream output;
    const auto result = ets2ap::test::RunCases({{"empty", [] {}}}, "", false, output);
    CHECK_EQ(result.tests, 1u);
    CHECK_EQ(result.checks, 0u);
    CHECK_NE(result.exit_code, 0);
}

ETS2AP_TEST_MAIN()
