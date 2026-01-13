#pragma once
#include <unordered_map>

class UserProfile {
    std::unordered_map<int, double> clickBoost;
public:
    void recordClick(int docId);
    double getBoost(int docId) const;
};
