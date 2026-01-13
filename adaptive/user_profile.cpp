#include "adaptive_ranker.h"

double AdaptiveRanker::score(double base, double userBoost) {
    return 0.7 * base + 0.3 * userBoost;
}
