#!/usr/bin/env python3
"""
scripts/generate_10k_corpus.py
Generates a realistic 10,000 document corpus (data/corpus_10k.txt)
combining computer science topics, technical domain vocabulary, and varied domain content.
"""

import sys
import os

OUT_PATH = "data/corpus_10k.txt"

templates = [
    "Artificial intelligence and machine learning algorithms are revolutionizing {topic} by optimizing data processing workflows.",
    "Deep neural networks and computer vision techniques enable automated feature extraction in {topic}.",
    "Natural language processing models analyze semantic structures and text patterns in {topic} documentation.",
    "Cloud computing infrastructure provides scalable serverless resources for high-throughput {topic} systems.",
    "Cybersecurity protocols protect distributed ledger architectures and privacy in modern {topic} platforms.",
    "Data science methodology integrates statistical inference, feature engineering, and predictive modeling for {topic}.",
    "Quantum computing algorithms leverage superposition to solve complex optimization problems in {topic}.",
    "Containerized microservices and DevOps pipelines streamline continuous integration for {topic} applications.",
    "Edge computing nodes process telemetry streams near the data source for real-time {topic} analytics.",
    "Database management systems use B-trees and LSM-trees to store and retrieve {topic} index records efficiently."
]

topics = [
    "autonomous vehicle navigation", "genomic sequence analysis", "high-frequency algorithmic trading",
    "smart grid energy distribution", "robotic surgical automation", "climate simulation modeling",
    "real-time fraud detection", "satellite remote sensing", "decentralized financial protocol",
    "industrial Internet of Things", "e-commerce recommendation engines", "medical imaging diagnostics",
    "smart city traffic management", "embedded IoT sensor networks", "predictive equipment maintenance",
    "augmented reality rendering", "distributed storage systems", "autonomous drone fleet routing",
    "semantic web knowledge graphs", "natural language translation"
]

modifiers = [
    "with sub-millisecond query execution performance",
    "ensuring strict fault tolerance and high availability",
    "utilizing GPU-accelerated tensor matrix operations",
    "integrated with zero-trust cryptographic access controls",
    "optimizing resource utilization across multi-region clusters",
    "evaluated on benchmark datasets using precision and recall metrics",
    "reducing memory footprint via vector compression algorithms",
    "deploying automated model retraining and drift monitoring",
    "implementing low-latency asynchronous message queues",
    "with linear scaling across distributed compute nodes"
]

print(f"Generating 10,000 document corpus to {OUT_PATH}...")

docs = []
doc_id = 1

while len(docs) < 10000:
    for t in templates:
        for top in topics:
            for mod in modifiers:
                doc = t.format(topic=top) + " " + mod + f" (Ref ID: {doc_id})"
                docs.append(doc)
                doc_id += 1
                if len(docs) >= 10000:
                    break
            if len(docs) >= 10000:
                break
        if len(docs) >= 10000:
            break

with open(OUT_PATH, "w", encoding="utf-8") as f:
    for d in docs:
        f.write(d + "\n")

print(f"✓ Generated {len(docs)} documents → {OUT_PATH} ({os.path.getsize(OUT_PATH) / 1024 / 1024:.2f} MB)")
