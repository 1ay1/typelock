#include "harness.hpp"

int main() {
    printf("\n\033[1mtypelock core tests\033[0m\n\n");

    // Tests are auto-registered by the TEST macro constructors
    // in each translation unit. By the time we reach main(), they've all run.

    printf("\n  \033[1m%d passed, %d failed\033[0m\n\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
