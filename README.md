# rx — High-Performance Streaming Text Processing Engine

`rx` is an ultra-fast, memory-bounded, zero-allocation-focused pipeline processor written in C17. It provides native regex extraction, token formatting, multi-stage substring mutation, and streaming SemVer aggregation in a single execution pass.

Unlike traditional shell pipes (`grep | sed | sort | head`) which incur heavy process-forking penalties and cross-boundary kernel buffer switching, `rx` operates strictly within a **single thread and single memory address space**. It is optimized to pull raw bytes directly off the disk or network and process massive multi-line tables, text dumps, or single-line minified payloads under flat resource caps.

---

## 🛠 Features & Engineering Breakthroughs

* **JIT-Compiled Regular Expressions**: Leverages native PCRE2 Just-In-Time compilation to compile targeted match patterns into raw machine code (assembly) at initialization, enabling hardware-speed scanning.
* **Deterministic Global Inline Matching**: Features a forced offset progression state engine that guarantees a strict forward march over lookahead sweeps. This makes inline infinite loop traps mathematically impossible—even when evaluated against chaotic alphanumeric noise (`[0-9a-z.]+`) or unescaped greedy patterns.
* **In-Memory Pipeline Matrix**: Implements a highly modular execution array routing logic using an efficient **Copy-on-Modify** strategy. The pipeline skips heap adjustments entirely unless target strings require active formatting or layout resizing.
* **Streaming Aggregator ($O(1)$ Space)**: Compares version elements on-the-fly using a real-time tournament logic. Lower priority components are thrown away immediately, keeping memory bounds flat whether processing 10 lines or 10 billion lines.
* **Robust SemVer Tokenizer**: Implements a non-destructive token comparison framework that structurally parses version strings component-by-component. Natively accommodates leading zeros, character patch flags (e.g., `1.1.1a` vs `1.1.1w`), and arbitrary separator changes (`.`, `_`, `-`).

---

## 📊 Structural Architecture

Instead of handling text data byte-by-byte or allocating loose lines dynamically, `rx` pulls incoming streams into dense **128KB static page blocks** via raw system calls.
