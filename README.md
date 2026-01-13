# Adaptive Search Engine (C++)

## Overview
This project implements a lightweight adaptive search engine in C++.
It combines classical information retrieval techniques with user behavior
signals (clicks and dwell time) to personalize search ranking.

The goal is to demonstrate system-level thinking similar to real-world
search engines used at scale.

## Core Components

### 1. Tokenizer
Normalizes and tokenizes raw text input into searchable terms.

### 2. Inverted Index
Maps tokens to document IDs for efficient lookup during search.

### 3. Ranking
Uses TF-IDF as a base relevance score.

### 4. Adaptive Ranking
Final score is adjusted using user interaction signals:
- Click frequency
- Dwell time

Final Score:
