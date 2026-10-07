#include "Euclidean.h"
#include <algorithm>

namespace plydrm
{

Pattern euclideanPattern (int steps, int hits) noexcept
{
    Pattern result;
    steps = std::clamp (steps, 0, kMaxPatternSteps);
    result.length = steps;

    if (steps == 0)
        return result;

    hits = std::clamp (hits, 0, steps);

    if (hits == 0)
        return result;

    if (hits == steps)
    {
        std::fill_n (result.step.begin(), steps, true);
        return result;
    }

    // Classic Bjorklund bucket construction: start with `hits` groups
    // containing a single "1" and `steps - hits` groups containing a single
    // "0", then repeatedly fold the remainder groups into the front groups
    // until at most one remainder group is left. Groups live in fixed arrays
    // (at most kMaxPatternSteps groups of at most kMaxPatternSteps steps).
    struct Group
    {
        std::array<bool, kMaxPatternSteps> bits {};
        int len = 0;
    };

    std::array<Group, kMaxPatternSteps> front {};
    std::array<Group, kMaxPatternSteps> back {};
    int numFront = hits;
    int numBack = steps - hits;

    for (int i = 0; i < numFront; ++i)
        front[static_cast<size_t> (i)] = { { true }, 1 };

    for (int i = 0; i < numBack; ++i)
        back[static_cast<size_t> (i)] = { { false }, 1 };

    while (numBack > 1)
    {
        const int pairs = std::min (numFront, numBack);

        for (int i = 0; i < pairs; ++i)
        {
            auto& f = front[static_cast<size_t> (i)];
            const auto& b = back[static_cast<size_t> (i)];
            std::copy_n (b.bits.begin(), b.len, f.bits.begin() + f.len);
            f.len += b.len;
        }

        // The groups that weren't paired become the new remainder. back[0..pairs)
        // were just folded into front, so their slots are free to overwrite.
        if (numFront > pairs)
        {
            for (int i = pairs; i < numFront; ++i)
                back[static_cast<size_t> (i - pairs)] = front[static_cast<size_t> (i)];
            numBack = numFront - pairs;
        }
        else if (numBack > pairs)
        {
            for (int i = pairs; i < numBack; ++i)
                back[static_cast<size_t> (i - pairs)] = back[static_cast<size_t> (i)];
            numBack -= pairs;
        }
        else
        {
            numBack = 0;
        }

        numFront = pairs;
    }

    int pos = 0;
    for (int i = 0; i < numFront; ++i)
    {
        const auto& g = front[static_cast<size_t> (i)];
        std::copy_n (g.bits.begin(), g.len, result.step.begin() + pos);
        pos += g.len;
    }
    for (int i = 0; i < numBack; ++i)
    {
        const auto& g = back[static_cast<size_t> (i)];
        std::copy_n (g.bits.begin(), g.len, result.step.begin() + pos);
        pos += g.len;
    }

    return result;
}

Pattern rotatePattern (const Pattern& pattern, int rotation) noexcept
{
    const int n = pattern.length;
    if (n == 0)
        return pattern;

    rotation = ((rotation % n) + n) % n;

    if (rotation == 0)
        return pattern;

    Pattern result;
    result.length = n;
    for (int i = 0; i < n; ++i)
        result.step[static_cast<size_t> (i)] = pattern.step[static_cast<size_t> ((i + rotation) % n)];

    return result;
}

} // namespace plydrm
