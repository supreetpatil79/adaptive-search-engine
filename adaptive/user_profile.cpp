#include "user_profile.h"

void UserProfile::recordClick(int docId) {
    // Increment click count; boost decays toward a ceiling of 1.0.
    // Formula: boost = 1 - exp(-λ * clicks), λ = 0.5
    clickCount[docId]++;
    int n = clickCount[docId];
    clickBoost[docId] = 1.0 - std::exp(-0.5 * static_cast<double>(n));
}

double UserProfile::getBoost(int docId) const {
    auto it = clickBoost.find(docId);
    if (it == clickBoost.end()) return 0.0;
    return it->second;
}
