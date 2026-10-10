#include <cstdio>

// Minimal ad-hoc test runner: each test file defines a `run<Name>Tests()`
// function and registers failures via the `check`/`checkEqual` macros in
// TestMain.h. Keeps the dependency footprint to just juce_core (already a
// transitive dependency of the plugin) instead of pulling in a full test
// framework for a handful of checks.
int plydrmTestsFailed = 0;

void runEuclideanTests();
void runPatternEngineTests();
void runSynthTests();
void runMacroTests();

int main()
{
    runEuclideanTests();
    runPatternEngineTests();
    runSynthTests();
    runMacroTests();

    if (plydrmTestsFailed == 0)
    {
        std::printf ("All Plydrm tests passed.\n");
        return 0;
    }

    std::printf ("%d Plydrm test check(s) FAILED.\n", plydrmTestsFailed);
    return 1;
}
