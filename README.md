# rx (it's AI written code)

> A high-performance, streaming text processor designed to replace complex shell pipelines with a single executable.

`rx` combines searching, extraction, replacement, and aggregation into a single streaming pipeline. Instead of spawning multiple processes (`grep`, `sed`, `awk`, `sort`, etc.), `rx` performs all operations in one pass while keeping memory usage low.

The project is designed around three principles:

- **Streaming** – process input without loading entire files into memory.
- **Composable** – chain multiple operations in one pipeline.
- **Fast** – minimize allocations and unnecessary copies.

---

# Features

- PCRE2 regular expressions (full syntax including capture groups, `\K`, lookarounds, Unicode)
- Streaming input processing (constant memory regardless of file size)
- Multiple pipeline stages (match → replace → replace → … → aggregate)
- Capture group extraction with custom format strings (`--format`)
- Literal-string replacement (not regex replacement) with `--replace`
- Aggregation stages: `--first`, `--max-version`
- SIMD-accelerated literal search (SSE4.2 / AVX2 via runtime CPU detection)
- PCRE2 JIT compilation for regex
- Zero temporary files
- Unix pipeline friendly (reads stdin, writes stdout)
- UTF-8 safe (through PCRE2)

---

# Why rx?

Typical shell pipelines often look like:

```bash
grep -Po 'Version:\s*\K[0-9.]+' file \
| sed 's/\.$//' \
    | sort -V \
    | tail -1
```

Each command creates:

- another process
- another parser
- another memory buffer
- another pipe

`rx` performs the same workflow inside one executable.

```bash
cat file \
| rx \
    --match 'Version:\s*\K[0-9.]+' \
    --replace '.' '' \
    --max-version
```

Everything happens in a single streaming pass.

---

# Design Goals

The project aims to be:

- extremely fast
- memory efficient
- allocation conscious
- predictable
- easy to extend

Unlike traditional text utilities, `rx` is built around an internal processing pipeline rather than individual commands.

---

# Architecture

Input flows through a sequence of processing stages.

```
stdin
   │
   ▼
Line Splitter (memchr on '\n')
   │
   ▼
Pipeline
 ├── Match (regex or literal)
 ├── Replace (literal)
 ├── Replace (literal)
 ├── ...
   │
   ▼
Aggregator (first | max-version | none)
   │
   ▼
stdout
```

Every line is processed independently. No stage needs to know about the others. Each stage receives a mutable string view and may:

- modify it
- reject it
- forward it

---

# Processing Pipeline

A pipeline consists of independent stages.

For example:

```
Match
   ↓
Replace
   ↓
Aggregate
```

or

```
Match
   ↓
Replace
   ↓
Replace
   ↓
Replace
   ↓
First
```

New stages can be added without changing the execution engine.

Every line is processed independently.

No stage needs to know about the others.

Each stage receives a mutable string view and may:

- modify it
- reject it
- forward it

---

# Processing Pipeline

A pipeline consists of independent stages.

For example:

```
Match
   ↓
Replace
   ↓
Aggregate
```

or

```
Match
   ↓
Replace
   ↓
Replace
   ↓
Replace
   ↓
First
```

New stages can be added without changing the execution engine.

---

### Example --format Usage

The `--format` option allows rearranging captured groups:

```bash
# Swap first and last names
cat names.txt | rx --match '(\\w+) (\\w+)' --format '\\2,\\1'
```

This transforms `John Doe` into `Doe, John`.

The format string can reference capture groups `\\1` through `\\9`:

```bash
# Extract domain from email addresses
cat emails.txt | rx --match '\\w+@(\\w+\\.\\w+)' --format '\\1'
```

---

# Streaming Model

Unlike tools that repeatedly scan the same data, `rx` processes input only once.

```
Input
 ↓
Read block
 ↓
Split into lines
 ↓
Run pipeline
 ↓
Output
```

Memory usage depends primarily on:

- read buffer
- current line
- stage contexts

It does **not** depend on file size.

Processing a 10 MB file and a 100 GB file uses essentially the same amount of memory.

---

# Current Pipeline Stages

## Match

Extracts text using PCRE2.

Example

```bash
rx --match 'Version:\s*\K[0-9.]+'
```

Supports:

- PCRE2 syntax
- capture groups
- `\K`
- lookarounds
- Unicode

---

## Replace

Performs literal text replacement (not regex replacement).

Example

```bash
rx \
    --match 'Version:\s*\K[0-9.]+' \
    --replace 'v' ''
```

Multiple replace stages may be chained.

---

## First

Returns only the first successful result.

Example

```bash
rx \
    --match '[0-9.]+' \
    --first
```

Equivalent to stopping after the first pipeline success.

---

## Max Version

Selects the highest semantic version encountered.

Example

```bash
rx \
    --match '[0-9]+(\.[0-9]+)+' \
    --max-version
```

Unlike lexical sorting, version components are compared numerically.

Example:

```
1.9
1.10
1.12
```

returns

```
1.12
```

---

# Example Workflows

Extract versions

```bash
cat versions.txt \
| rx --match 'Version:\s*\K[0-9.]+'
```

---

Extract and normalize

```bash
cat versions.txt \
| rx \
    --match 'Version:\s*\K[0-9.]+' \
    --replace '\.$' ''
```

---

Find latest version

```bash
cat versions.txt \
| rx \
    --match '[0-9]+(\.[0-9]+)+' \
    --max-version
```

---

Extract first match only

```bash
cat log.txt \
| rx \
    --match 'ERROR:.*' \
    --first
```

---

# Performance Philosophy

`rx` is designed to minimize unnecessary work.

Instead of

```
read
 ↓
grep
 ↓
write
 ↓
sed
 ↓
write
 ↓
sort
 ↓
write
```

the program performs

```
read
 ↓
match
 ↓
replace
 ↓
aggregate
 ↓
write
```

No intermediate files.

No repeated parsing.

No repeated process startup.

---

# Memory Usage

The implementation is designed for bounded memory usage.

Major allocations include:

- input buffer
- regex contexts
- pipeline contexts

Memory usage remains nearly constant regardless of input size.

---

# Repository Layout

```
src/
    aggregate.c
    aggregate.h
    match.c
    match.h
    pipeline.c
    pipeline.h
    replace.c
    replace.h
    string_view.c
    string_view.h
    main.c

CMakeLists.txt
README.md
```

---

# Building

Requirements

- C99 compiler
- CMake
- PCRE2 (8-bit library)

Example

```bash
mkdir build
cd build

cmake ..
cmake --build .
```

---

# Roadmap

Planned features include:

- additional aggregation operators
- multiple output formats
- field extraction
- delimiter support
- numeric comparisons
- JSON extraction
- streaming statistics
- configurable buffering
- colorized output
- SIMD optimizations
- PCRE2 JIT improvements
- plugin pipeline stages

---

# Philosophy

`rx` is not intended to replace every Unix text utility.

Instead, it focuses on the common case where several text-processing commands are chained together. By performing those operations inside a single streaming engine, it aims to reduce process overhead, memory usage, and repeated parsing while remaining simple, composable, and predictable.

---

# License

MIT License
