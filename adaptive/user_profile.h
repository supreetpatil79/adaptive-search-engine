#pragma once
#include <cmath>
#include <unordered_map>

// Tracks per-document click history for a single session.
// Boost saturates toward 1.0 using an exponential model so repeated
// clicks add diminishing returns: boost = 1 - exp(-0.5 * clicks).
class UserProfile {
    std::unordered_map<int, int>    clickCount;  // docId -> raw click count
    std::unordered_map<int, double> clickBoost;  // docId -> [0, 1) boost factor

public:
    void   recordClick(int docId);
    double getBoost(int docId) const;
};
