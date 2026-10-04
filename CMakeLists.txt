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
if(NOT DEFINED ORT_ROOT)
    file(GLOB ORT_CANDIDATES
        "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-osx-arm64-*"
        "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-linux-x64-*"
        "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-*"
    )
    if(ORT_CANDIDATES)
        list(GET ORT_CANDIDATES 0 ORT_ROOT)
    else()
        if(APPLE)
            set(ORT_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-osx-arm64-1.28.0")
        else()
            set(ORT_ROOT "${CMAKE_SOURCE_DIR}/third_party/onnxruntime-linux-x64-1.18.0")
        endif()
    endif()
endif()

if(NOT DEFINED ORT_INCLUDE_DIR)
    set(ORT_INCLUDE_DIR "${ORT_ROOT}/include")
endif()

if(NOT DEFINED ORT_LIB)
    if(APPLE)
        set(ORT_LIB "${ORT_ROOT}/lib/libonnxruntime.dylib")
    else()
        set(ORT_LIB "${ORT_ROOT}/lib/libonnxruntime.so")
    endif()
endif()

get_filename_component(ORT_LIB_FILENAME "${ORT_LIB}" NAME)

if(NOT EXISTS "${ORT_INCLUDE_DIR}/onnxruntime_cxx_api.h")
    message(FATAL_ERROR "ONNX Runtime headers not found at ${ORT_INCLUDE_DIR}.\n"
            "Download ONNX Runtime into third_party/ or set ORT_ROOT.")
endif()

# ── Core engine sources (no ORT dependency) ────────────────────────────────
set(CORE_SOURCES
    ranking/bm25.cpp
    ranking/tfidf.cpp
    ranking/wand_scorer.cpp
    index/inverted_index.cpp
    index/concurrent_index.cpp
    index/segment_builder.cpp
    index/wal.cpp
    index/shard_manager.cpp
    index/metadata_index.cpp
    tokenizer/tokenizer.cpp
    query/stemmer.cpp
    query/spell_checker.cpp
    query/snippet_generator.cpp
    query/prefix_trie.cpp
    adaptive/adaptive_ranker.cpp
    adaptive/user_profile.cpp
    search/search_engine.cpp
    utils/file_loader.cpp
    utils/vbyte.cpp
)

# ── Embed module sources (require ORT) ────────────────────────────────────
set(EMBED_SOURCES
    embed/flat_embed_index.cpp
    embed/hnsw_index.cpp
    embed/ort_embedder.cpp
    embed/rrf_fusion.cpp
    embed/sq8_index.cpp
    ranking/cross_encoder.cpp
    server/search_server.cpp
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
    ${CMAKE_SOURCE_DIR}/server
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

# Copy ONNX Runtime library next to the binary so it can be found at runtime.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ORT_LIB}"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/${ORT_LIB_FILENAME}"
    COMMENT "Copying ${ORT_LIB_FILENAME} alongside executable"
)

# ── REST HTTP Server executable ───────────────────────────────────────────
add_executable(adaptive-search-server
    src/server_main.cpp
    ${CORE_SOURCES}
    ${EMBED_SOURCES}
)
target_include_directories(adaptive-search-server PRIVATE ${ENGINE_INCLUDES})
target_link_libraries(adaptive-search-server PRIVATE "${ORT_LIB}")

add_custom_command(TARGET adaptive-search-server POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ORT_LIB}"
        "$<TARGET_FILE_DIR:adaptive-search-server>/${ORT_LIB_FILENAME}"
    COMMENT "Copying ${ORT_LIB_FILENAME} alongside server executable"
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

# Copy ONNX Runtime library next to evaluator binary
add_custom_command(TARGET evaluator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ORT_LIB}"
        "$<TARGET_FILE_DIR:evaluator>/${ORT_LIB_FILENAME}"
    COMMENT "Copying ${ORT_LIB_FILENAME} alongside evaluator"
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
install(TARGETS ${PROJECT_NAME} adaptive-search-server build_hnsw DESTINATION bin)

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

# ── Test: Inverted Index correctness ───────────────────────────────────────
add_executable(test_inverted_index
    tests/test_inverted_index.cpp
    ${CORE_SOURCES}
)
target_include_directories(test_inverted_index PRIVATE ${ENGINE_INCLUDES})

# ── Test: SQ8 Scalar Quantization ──────────────────────────────────────────
add_executable(test_sq8
    tests/test_sq8.cpp
    embed/sq8_index.cpp
)
target_include_directories(test_sq8 PRIVATE ${ENGINE_INCLUDES})

# ── Test: Snippet Generation & Highlighting ────────────────────────────────
add_executable(test_snippet
    tests/test_snippet.cpp
    ${CORE_SOURCES}
)
target_include_directories(test_snippet PRIVATE ${ENGINE_INCLUDES})

# ── Test: Distributed Shards & WAL ─────────────────────────────────────────
add_executable(test_shards
    tests/test_shards.cpp
    ${CORE_SOURCES}
)
target_include_directories(test_shards PRIVATE ${ENGINE_INCLUDES})

# ── Test: Cross-Encoder Neural Re-Ranker ───────────────────────────────────
add_executable(test_cross_encoder
    tests/test_cross_encoder.cpp
    ${CORE_SOURCES}
    ${EMBED_SOURCES}
)
target_include_directories(test_cross_encoder PRIVATE ${ENGINE_INCLUDES})
target_link_libraries(test_cross_encoder PRIVATE "${ORT_LIB}")

add_custom_command(TARGET test_cross_encoder POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${ORT_LIB}"
        "$<TARGET_FILE_DIR:test_cross_encoder>/${ORT_LIB_FILENAME}"
    COMMENT "Copying ${ORT_LIB_FILENAME} alongside test_cross_encoder"
)

# ── Test: PrefixTrie Autocomplete ──────────────────────────────────────────
add_executable(test_prefix_trie
    tests/test_prefix_trie.cpp
    ${CORE_SOURCES}
)
target_include_directories(test_prefix_trie PRIVATE ${ENGINE_INCLUDES})

# ── Test: Metadata Index & Filtered HNSW ────────────────────────────────────
add_executable(test_metadata
    tests/test_metadata.cpp
    ${CORE_SOURCES}
    embed/hnsw_index.cpp
)
target_include_directories(test_metadata PRIVATE ${ENGINE_INCLUDES})

# ── CTest integration ─────────────────────────────────────────────────────
# Run: cmake --build build && ctest --test-dir build --output-on-failure
enable_testing()
add_test(NAME rrf_unit_tests            COMMAND test_rrf)
add_test(NAME concurrency_test          COMMAND test_concurrency)
add_test(NAME stemmer_unit_test         COMMAND test_stemmer)
add_test(NAME simd_unit_test            COMMAND test_simd)
add_test(NAME inverted_index_unit_test  COMMAND test_inverted_index)
add_test(NAME sq8_unit_test             COMMAND test_sq8)
add_test(NAME snippet_unit_test         COMMAND test_snippet)
add_test(NAME shards_unit_test          COMMAND test_shards)
add_test(NAME cross_encoder_unit_test   COMMAND test_cross_encoder)
add_test(NAME prefix_trie_unit_test     COMMAND test_prefix_trie)
add_test(NAME metadata_unit_test        COMMAND test_metadata)

# Tests that need data/documents.txt must run from project root
set_tests_properties(rrf_unit_tests            PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(concurrency_test          PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" TIMEOUT 30)
set_tests_properties(stemmer_unit_test         PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(simd_unit_test            PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(inverted_index_unit_test  PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(sq8_unit_test             PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(snippet_unit_test         PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(shards_unit_test          PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(cross_encoder_unit_test   PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(prefix_trie_unit_test     PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
set_tests_properties(metadata_unit_test        PROPERTIES WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# ── Build info ────────────────────────────────────────────────────────────
message(STATUS "Compiler    : ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "ORT include : ${ORT_INCLUDE_DIR}")
message(STATUS "ORT lib     : ${ORT_LIB}")
message(STATUS "Sanitizers  : ${ENABLE_SANITIZERS}")