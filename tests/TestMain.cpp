#include "TestSuites.h"

#include <cstring>
#include <iostream>

int main (int argc, char* argv[])
{
    struct Suite
    {
        const char* name;
        int (*run)();
    };

    const Suite suites[] = { { "engine", runEngineTests },
                             { "analysis", runAnalysisTests },
                             { "material", runMaterialTests },
                             { "plugin", runPluginTests } };

    // Rendering the interface is a separate job from the suites, and never part of a test run.
    if (argc > 2 && std::strcmp (argv[1], "shots") == 0)
        return writePressShots (argv[2], argc > 3 ? argv[3] : "");

    auto ran = 0;
    auto failed = 0;

    for (const auto& suite : suites)
    {
        if (argc > 1 && std::strcmp (argv[1], suite.name) != 0)
            continue;

        ++ran;
        failed += suite.run() == 0 ? 0 : 1;
    }

    if (ran == 0)
    {
        std::cerr << "Unknown suite: " << argv[1] << std::endl;
        return 2;
    }

    return failed == 0 ? 0 : 1;
}
