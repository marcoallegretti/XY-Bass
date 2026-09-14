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
