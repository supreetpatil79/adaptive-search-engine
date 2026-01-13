cmake_minimum_required(VERSION 3.10)
project(adaptive-search-engine)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-Wall -Wextra -pedantic)
endif()

# Source files
set(SOURCES
    src/main_simple.cpp
)

# Header files (for reference, not required for compilation)
set(HEADERS
    tokenizer/tokenizer.h
    adaptive/adaptive_ranker.h
    index/inverted_index.h
    ranking/bm25.h
    ranking/tfidf.h
    utils/file_loader.h
)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/adaptive
    ${CMAKE_SOURCE_DIR}/index
    ${CMAKE_SOURCE_DIR}/ranking
    ${CMAKE_SOURCE_DIR}/utils
    ${CMAKE_SOURCE_DIR}/tokenizer
)

# Create executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Installation
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

# Print configuration
message(STATUS "C++ Compiler: ${CMAKE_CXX_COMPILER}")
message(STATUS "C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")