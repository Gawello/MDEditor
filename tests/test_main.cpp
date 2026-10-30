#include "TestHarness.h"

int main()
{
    for (const auto &c : test::registry()) {
        const int before = test::failures();
        c.fn();
        std::cout << (test::failures() == before ? "[ OK ] " : "[FAIL] ") << c.name << "\n";
    }
    std::cout << test::registry().size() << " test cases, " << test::failures() << " failed checks\n";
    return test::failures() == 0 ? 0 : 1;
}
