#include "segment_builder.h"
#include <algorithm>

SegmentBuilder::SegmentBuilder(size_t numThreads)
    : numWorkerThreads(numThreads > 0 ? numThreads : 4) {}

void SegmentBuilder::buildParallel(
    const std::vector<Document>& docs,
    ConcurrentInvertedIndex& targetIndex
) {
    if (docs.empty()) return;

    size_t totalDocs = docs.size();
    size_t threadsToUse = std::min(numWorkerThreads, totalDocs);
    size_t chunkSize = (totalDocs + threadsToUse - 1) / threadsToUse;

    std::vector<InvertedIndex> segments(threadsToUse);
    std::vector<std::thread> workers;
    workers.reserve(threadsToUse);

    // Phase 1: Parallel indexing into independent segments (lock-free)
    for (size_t t = 0; t < threadsToUse; ++t) {
        size_t startIdx = t * chunkSize;
        size_t endIdx = std::min(startIdx + chunkSize, totalDocs);

        workers.emplace_back([&docs, &segments, t, startIdx, endIdx]() {
            for (size_t i = startIdx; i < endIdx; ++i) {
                segments[t].addDocument(docs[i]);
            }
            segments[t].finalize();
        });
    }

    // Join all workers
    for (auto& worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    // Phase 2: Merge thread-local segments into target index
    for (size_t t = 0; t < threadsToUse; ++t) {
        targetIndex.mergeSegment(segments[t]);
    }

    // Phase 3: Finalize merged target index
    targetIndex.finalize();
}
