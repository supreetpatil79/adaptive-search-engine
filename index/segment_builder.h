#ifndef SEGMENT_BUILDER_H
#define SEGMENT_BUILDER_H

#include "concurrent_index.h"
#include <vector>
#include <thread>
#include <functional>

// Segment-based Parallel Index Builder.
// Divides a collection of documents across N worker threads.
// Each worker thread tokenizes and indexes its partition independently into a thread-local segment.
// Segments are then merged concurrently into the target ConcurrentInvertedIndex.

class SegmentBuilder {
public:
    explicit SegmentBuilder(size_t numThreads = std::thread::hardware_concurrency());

    // Build index in parallel from a vector of raw documents
    void buildParallel(
        const std::vector<Document>& docs,
        ConcurrentInvertedIndex& targetIndex
    );

private:
    size_t numWorkerThreads;
};

#endif // SEGMENT_BUILDER_H
