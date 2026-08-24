cmake_minimum_required(VERSION 3.14)
project(adaptive-search-engine VERSION 0.2.0 LANGUAGES CXX)

# ── Language standard ──────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler warnings ──────────────────────────────────────────────────────
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    add_compile_options(
        -Wall -Wextra -Wpedantic
        -Wshadow
        -Wno-unused-parameter
    )
endif()

# ── Sanitizers (Debug only) ────────────────────────────────────────────────
option(ENABLE_SANITIZERS "Build with ASan + UBSan" OFF)
if(ENABLE_SANITIZERS AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# ── ONNX Runtime ──────────────────────────────────────────────────────────
# Prebuilt SDK downloaded to third_party/onnxruntime-osx-arm64-1.28.0/
set(ORT_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-osx-arm64-1.28.0")
set(ORT_INCLUDE_DIR "${ORT_ROOT}/include")
set(ORT_LIB "${ORT_ROOT}/lib/libonnxruntime.dylib")

if(NOT EXISTS "${ORT_INCLUDE_DIR}/onnxruntime_cxx_api.h")
    message(FATAL_ERROR "ONNX Runtime headers not found at ${ORT_INCLUDE_DIR}.\n"
            "Run: ./scripts/setup_ort.sh  or download manually.")
endif()

# ── Core engine sources (no ORT dependency) ────────────────────────────────
set(CORE_SOURCES
    ranking/bm25.cpp
    ranking/tfidf.cpp
    ranking/wand_scorer.cpp
    index/inverted_index.cpp
    index/concurrent_index.cpp
    index/segment_builder.cpp
    tokenizer/tokenizer.cpp
    query/stemmer.cpp
    query/spell_checker.cpp
    adaptive/adaptive_ranker.cpp
    adaptive/user_profile.cpp
    search/search_engine.cpp
    utils/file_loader.cpp
)

# ── Embed module sources (require ORT) ────────────────────────────────────
set(EMBED_SOURCES
    embed/flat_embed_index.cpp
    embed/hnsw_index.cpp
    embed/ort_embedder.cpp
    embed/rrf_fusion.cpp
)

# ── Include directories ────────────────────────────────────────────────────
set(ENGINE_INCLUDES
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/adaptive
    ${CMAKE_SOURCE_DIR}/cache
    ${CMAKE_SOURCE_DIR}/embed
    ${CMAKE_SOURCE_DIR}/index
    ${CMAKE_SOURCE_DIR}/query
    ${CMAKE_SOURCE_DIR}/ranking
    ${CMAKE_SOURCE_DIR}/search
    ${CMAKE_SOURCE_DIR}/tokenizer
    ${CMAKE_SOURCE_DIR}/utils
    ${ORT_INCLUDE_DIR}
)

# ── Main executable ────────────────────────────────────────────────────────
add_executable(${PROJECT_NAME}
    src/main.cpp
    ${CORE_SOURCES}
    ${EMBED_SOURCES}
)
target_include_directories(${PROJECT_NAME} PRIVATE ${ENGINE_INCLUDES})
target_link_libraries(${PROJECT_NAME} PRIVATE "${ORT_LIB}")

# Copy libonnxruntime.dylib next to the binary so it can be found at runtime.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ORT_LIB}"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/libonnxruntime.dylib"
    COMMENT "Copying libonnxruntime.dylib alongside executable"
)

# ── Benchmark: WAND Pruning vs Unpruned ────────────────────────────────────
add_executable(benchmark_pruning
    bench/benchmark_pruning.cpp
    ${CORE_SOURCES}
)
target_include_directories(benchmark_pruning PRIVATE ${ENGINE_INCLUDES})

# ── Benchmark: Query Throughput QPS ────────────────────────────────────────
add_executable(benchmark_throughput
    bench/benchmark_throughput.cpp
    ${CORE_SOURCES}
)
target_include_directories(benchmark_throughput PRIVATE ${ENGINE_INCLUDES})

# ── Benchmark: SIMD Vector Math Speedup ────────────────────────────────────
add_executable(benchmark_simd
    bench/benchmark_simd.cpp
)
target_include_directories(benchmark_simd PRIVATE ${ENGINE_INCLUDES})


# ── Test: Multi-threaded Concurrency & Stress Test ─────────────────────────
add_executable(test_concurrency
    tests/test_concurrency.cpp
    ${CORE_SOURCES}
)
target_include_directories(test_concurrency PRIVATE ${ENGINE_INCLUDES})

# ── Evaluation: NDCG@10 Evaluator ───────────────────────────────────────────
add_executable(evaluator
    eval/evaluator.cpp
    ${CORE_SOURCES}
    ${EMBED_SOURCES}
)
target_include_directories(evaluator PRIVATE ${ENGINE_INCLUDES})
target_link_libraries(evaluator PRIVATE "${ORT_LIB}")

# Copy libonnxruntime.dylib next to evaluator binary
add_custom_command(TARGET evaluator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ORT_LIB}"
        "$<TARGET_FILE_DIR:evaluator>/libonnxruntime.dylib"
    COMMENT "Copying libonnxruntime.dylib alongside evaluator"
)

# ── Test: RRF unit tests (no ORT — tests pure fusion logic) ───────────────
add_executable(test_rrf
    tests/test_rrf.cpp
    embed/rrf_fusion.cpp
    ${CORE_SOURCES}
)
target_include_directories(test_rrf PRIVATE ${ENGINE_INCLUDES})

# ── Tool: Build HNSW graph from embeddings.bin ─────────────────────────────
add_executable(build_hnsw
    embed/build_hnsw.cpp
    embed/flat_embed_index.cpp
    embed/hnsw_index.cpp
)
target_include_directories(build_hnsw PRIVATE ${ENGINE_INCLUDES})

# ── Installation ───────────────────────────────────────────────────────────
install(TARGETS ${PROJECT_NAME} build_hnsw DESTINATION bin)

# ── Test: Porter Stemmer unit tests ────────────────────────────────────────
add_executable(test_stemmer
    tests/test_stemmer.cpp
    query/stemmer.cpp
)
target_include_directories(test_stemmer PRIVATE ${ENGINE_INCLUDES})

# ── Test: SIMD inner-product correctness ───────────────────────────────────
add_executable(test_simd
    tests/test_simd.cpp
)
target_include_directories(test_simd PRIVATE ${ENGINE_INCLUDES})

# ── CTest integration ─────────────────────────────────────────────────────
# Run: cmake --build build && ctest --test-dir build --output-on-failure
enable_testing()
add_test(NAME rrf_unit_tests    COMMAND test_rrf)
add_test(NAME concurrency_test  COMMAND test_concurrency)
add_test(NAME stemmer_unit_test COMMAND test_stemmer)
add_test(NAME simd_unit_test    COMMAND test_simd)
# Tests that need data/documents.txt must run from project root
set_tests_properties(rrf_unit_tests   PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(concurrency_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 30)
set_tests_properties(stemmer_unit_test PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(simd_unit_test    PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")


# ── Build info ────────────────────────────────────────────────────────────
message(STATUS "Compiler    : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "ORT include : ${ORT_INCLUDE_DIR}")
message(STATUS "ORT lib     : ${ORT_LIB}")
message(STATUS "Sanitizers  : ${ENABLE_SANITIZERS}")