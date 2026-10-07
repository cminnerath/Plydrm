#include "Euclidean.h"
#include <algorithm>

namespace plydrm
{

std::vector<bool> euclideanPattern (int steps, int hits)
{
    if (steps <= 0)
        return {};

    hits = std::clamp (hits, 0, steps);

    if (hits == 0)
        return std::vector<bool> (static_cast<size_t> (steps), false);

    if (hits == steps)
        return std::vector<bool> (static_cast<size_t> (steps), true);

    // Classic Bjorklund bucket construction: start with `hits` groups
    // containing a single "1" and `steps - hits` groups containing a single
    // "0", then repeatedly fold the remainder groups into the front groups
    // until at most one remainder group is left.
    std::vector<std::vector<bool>> front;
    std::vector<std::vector<bool>> back;

    front.reserve (static_cast<size_t> (hits));
    back.reserve (static_cast<size_t> (steps - hits));

    for (int i = 0; i < hits; ++i)
        front.push_back ({ true });

    for (int i = 0; i < steps - hits; ++i)
        back.push_back ({ false });

    while (back.size() > 1)
    {
        const size_t pairs = std::min (front.size(), back.size());

        std::vector<std::vector<bool>> newFront;
        newFront.reserve (pairs);

        for (size_t i = 0; i < pairs; ++i)
        {
            auto combined = front[i];
            combined.insert (combined.end(), back[i].begin(), back[i].end());
            newFront.push_back (std::move (combined));
        }

        std::vector<std::vector<bool>> newBack;

        if (front.size() > pairs)
            newBack.assign (front.begin() + static_cast<long> (pairs), front.end());
        else if (back.size() > pairs)
            newBack.assign (back.begin() + static_cast<long> (pairs), back.end());

        front = std::move (newFront);
        back = std::move (newBack);
    }

    std::vector<bool> result;
    result.reserve (static_cast<size_t> (steps));

    for (auto& g : front)
        result.insert (result.end(), g.begin(), g.end());
    for (auto& g : back)
        result.insert (result.end(), g.begin(), g.end());

    return result;
}

std::vector<bool> rotatePattern (const std::vector<bool>& pattern, int rotation)
{
    if (pattern.empty())
        return pattern;

    const int n = static_cast<int> (pattern.size());
    rotation = ((rotation % n) + n) % n;

    if (rotation == 0)
        return pattern;

    std::vector<bool> result (pattern.size());
    for (int i = 0; i < n; ++i)
        result[static_cast<size_t> (i)] = pattern[static_cast<size_t> ((i + rotation) % n)];

    return result;
}

} // namespace plydrm
