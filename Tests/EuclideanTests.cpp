#include "TestMain.h"
#include "../Source/Engine/Euclidean.h"
#include <numeric>

using namespace plydrm;

namespace
{
    int countTrue (const std::vector<bool>& v)
    {
        return std::accumulate (v.begin(), v.end(), 0, [] (int acc, bool b) { return acc + (b ? 1 : 0); });
    }

    std::string toString (const std::vector<bool>& v)
    {
        std::string s;
        for (bool b : v)
            s += (b ? 'x' : '.');
        return s;
    }
}

void runEuclideanTests()
{
    // Classic E(3,8) tresillo pattern.
    {
        auto p = euclideanPattern (8, 3);
        PLYDRM_CHECK (p.size() == 8, "E(3,8) should have 8 steps");
        PLYDRM_CHECK (countTrue (p) == 3, "E(3,8) should have 3 hits, got " + toString (p));
    }

    // E(5,8) - common Cuban cinquillo-ish distribution.
    {
        auto p = euclideanPattern (8, 5);
        PLYDRM_CHECK (countTrue (p) == 5, "E(5,8) should have 5 hits, got " + toString (p));
    }

    // Edge cases.
    {
        auto zero = euclideanPattern (16, 0);
        PLYDRM_CHECK (countTrue (zero) == 0, "E(0,16) should have no hits");
        PLYDRM_CHECK (zero.size() == 16, "E(0,16) should still have 16 steps");

        auto full = euclideanPattern (16, 16);
        PLYDRM_CHECK (countTrue (full) == 16, "E(16,16) should be all hits");

        auto empty = euclideanPattern (0, 0);
        PLYDRM_CHECK (empty.empty(), "E(0,0) should be empty");
    }

    // Hits greater than steps should clamp rather than crash.
    {
        auto clamped = euclideanPattern (4, 10);
        PLYDRM_CHECK (clamped.size() == 4, "hits > steps should clamp pattern length to steps");
        PLYDRM_CHECK (countTrue (clamped) == 4, "hits > steps should clamp to all steps active");
    }

    // Rotation should preserve the hit count and wrap correctly.
    {
        auto p = euclideanPattern (8, 3);
        auto rotated = rotatePattern (p, 3);
        PLYDRM_CHECK (countTrue (rotated) == countTrue (p), "rotation must preserve hit count");

        auto rotatedFull = rotatePattern (p, static_cast<int> (p.size()));
        PLYDRM_CHECK (rotatedFull == p, "rotating by the full length should be a no-op");
    }
}
