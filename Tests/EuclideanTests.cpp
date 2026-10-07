#include "TestMain.h"
#include "../Source/Engine/Euclidean.h"

using namespace plydrm;

namespace
{
    int countTrue (const Pattern& p)
    {
        int n = 0;
        for (int i = 0; i < p.length; ++i)
            n += p[i] ? 1 : 0;
        return n;
    }

    std::string toString (const Pattern& p)
    {
        std::string s;
        for (int i = 0; i < p.length; ++i)
            s += (p[i] ? 'x' : '.');
        return s;
    }

    void checkPattern (int steps, int hits, const std::string& expected)
    {
        const auto got = toString (euclideanPattern (steps, hits));
        PLYDRM_CHECK (got == expected, "E(" + std::to_string (hits) + "," + std::to_string (steps) + ") should be "
                                           + expected + ", got " + got);
    }
}

void runEuclideanTests()
{
    // Exact Bjorklund distributions (classic rhythms).
    checkPattern (8, 3, "x..x..x.");             // tresillo
    checkPattern (8, 5, "x.xx.xx.");             // cinquillo
    checkPattern (16, 4, "x...x...x...x...");    // four on the floor
    checkPattern (12, 7, "x.xx.x.xx.x.");
    checkPattern (13, 5, "x..x.x..x.x..");
    checkPattern (16, 9, "x.xx.x.x.xx.x.x.");

    // Every steps/hits combination has the right length and hit count.
    for (int steps = 1; steps <= kMaxPatternSteps; ++steps)
        for (int hits = 0; hits <= steps; ++hits)
        {
            const auto p = euclideanPattern (steps, hits);
            if (p.length != steps || countTrue (p) != hits)
                PLYDRM_CHECK (false, "E(" + std::to_string (hits) + "," + std::to_string (steps) + ") is " + toString (p));
        }

    // Edge cases.
    {
        auto zero = euclideanPattern (16, 0);
        PLYDRM_CHECK (countTrue (zero) == 0, "E(0,16) should have no hits");
        PLYDRM_CHECK (zero.length == 16, "E(0,16) should still have 16 steps");

        auto full = euclideanPattern (16, 16);
        PLYDRM_CHECK (countTrue (full) == 16, "E(16,16) should be all hits");

        auto empty = euclideanPattern (0, 0);
        PLYDRM_CHECK (empty.length == 0, "E(0,0) should be empty");

        auto tooLong = euclideanPattern (40, 5);
        PLYDRM_CHECK (tooLong.length == kMaxPatternSteps, "steps beyond the maximum should clamp to it");
    }

    // Hits greater than steps should clamp rather than crash.
    {
        auto clamped = euclideanPattern (4, 10);
        PLYDRM_CHECK (clamped.length == 4, "hits > steps should clamp pattern length to steps");
        PLYDRM_CHECK (countTrue (clamped) == 4, "hits > steps should clamp to all steps active");
    }

    // Rotation shifts left, preserves the hit count and wraps correctly.
    {
        auto p = euclideanPattern (8, 3);
        auto rotated = rotatePattern (p, 3);
        PLYDRM_CHECK (toString (rotated) == "x..x.x..", "E(3,8) rotated by 3 should be x..x.x.., got " + toString (rotated));
        PLYDRM_CHECK (countTrue (rotated) == countTrue (p), "rotation must preserve hit count");

        PLYDRM_CHECK (rotatePattern (p, p.length) == p, "rotating by the full length should be a no-op");
        PLYDRM_CHECK (rotatePattern (p, -1) == rotatePattern (p, 7), "negative rotation should wrap");
    }
}
