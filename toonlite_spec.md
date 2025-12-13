# toonlite — Product + Technical Specification (v1)

**Package mission:** a *minimal-dependency*, *performance-first* R package for reading, writing, validating, streaming, and converting **TOON** (Token-Oriented Object Notation) data, with a focus on very large tabular files and robust diagnostics.

This document is written as an implementation blueprint intended to be “one-shot” consumable by an autonomous coding agent (e.g., Claude Code). It specifies **public API**, **behavioral contracts**, **edge-case policies**, **C++ architecture**, **performance targets**, and **test/CI requirements**.

---

## 0) Guiding principles

1. **Performance is a feature.** Optimize for large files and long-running pipelines.
2. **Minimal dependencies by default.** Base R + compiled C++ only.
   - Optional features (Parquet/Feather via `arrow`, JSON convenience via `jsonlite`) go in `Suggests`, never `Imports`.
3. **Explicit user-facing API.** Prefer clear verbs and explicit conversions.
4. **Strict core, controlled extensions.**
   - `strict=TRUE` enforces the TOON spec as the baseline.
   - Some pragmatic extensions (notably comments, duplicate keys, row-length mismatch) are **handled permissively by default** as specified below.
5. **Deterministic results.** Stable formatting and stable type inference rules.
6. **Good errors.** Every parse error must identify file/line/column and show a snippet when possible.

---

## 1) Product scope

### 1.1 Included in v1 (Definition of Done)

- **General TOON I/O**
  - Parse TOON from file or string to R (nested lists/vectors).
  - Serialize R objects to TOON (string/file).
- **Validation & formatting**
  - `validate_toon()` boolean validator (returns TRUE/FALSE, attaches error details on FALSE).
  - `assert_toon()` throws on invalid input.
  - `format_toon()` pretty-printer, `canonical` option for stable diffs.
- **Tabular fast path**
  - Decode tabular arrays into base `data.frame` efficiently.
  - Encode `data.frame` to tabular TOON.
- **Streaming**
  - Stream tabular rows in batches to a callback without materializing the whole dataset.
  - Stream non-tabular array items in batches (list batches).
- **Conversions**
  - Lossless lane: TOON ↔ JSON (JSON data model).
  - Tabular lane: TOON ↔ CSV (and optionally Parquet/Feather if `arrow` installed).
- **Conformance**
  - CI runs official TOON conformance fixtures (decode/encode/error/strict-mode) where applicable.

### 1.2 Non-goals (v1)

- No background async I/O scheduling, no progress bars by default.
- No automatic `tibble` return type by default (base `data.frame` only).
- No mmap requirement (keep portable; use buffered I/O).
- No schema registry or Arrow Dataset integration beyond straightforward file conversions.

---

## 2) Public API surface

All functions are **exported**. Names are all-lowercase, snake_case, tidyverse-adjacent but dependency-free.

### 2.1 Core: in-memory parse/serialize

#### `from_toon(text, *, strict = TRUE, simplify = TRUE, allow_comments = TRUE, allow_duplicate_keys = TRUE)`
- **Input:** `text` character scalar (or raw vector; if raw, treated as UTF-8 bytes).
- **Output:** R object representing JSON data model:
  - object → named list
  - array → list or atomic vector if `simplify=TRUE` and homogeneous primitives
  - primitives → logical/integer/double/character/NULL
- **Errors:** If parse fails and `strict=TRUE`, throws `toonlite_parse_error` condition.

#### `to_toon(x, *, pretty = TRUE, indent = 2L, strict = TRUE, allow_comments = FALSE)`
- **Input:** R object.
- **Output:** character scalar with class `"toon"`.
- **Normalization (write-side):**
  - `factor` → character
  - `Date`/`POSIXct` → ISO 8601 string by default (see §5.4)
  - `NaN/Inf/-Inf` in numeric vectors: error when `strict=TRUE` (see §5.3)
- **Pretty:** When `pretty=TRUE`, multi-line formatting with indentation.
- **Note:** `allow_comments` exists only for future use (writers generally should not emit comments).

### 2.2 Core: file I/O

#### `read_toon(file, *, strict = TRUE, simplify = TRUE, allow_comments = TRUE, allow_duplicate_keys = TRUE, encoding = "UTF-8")`
- Like `from_toon()` but reads from a file path.

#### `write_toon(x, file, *, pretty = TRUE, indent = 2L, strict = TRUE)`
- Like `to_toon()` but writes to a file.

### 2.3 Validation & formatting

#### `validate_toon(x, *, is_file = FALSE, strict = TRUE, allow_comments = TRUE, allow_duplicate_keys = TRUE)`
- **Input:** `x` is either TOON text (default) or a file path if `is_file=TRUE`.
- **Output:** logical scalar.
  - TRUE if valid.
  - FALSE if invalid, with attribute `attr(result, "error")` containing a structured error object:
    - `list(type, message, line, column, snippet, file)`
- **Never throws** unless internal error (e.g., file unreadable): those are separate I/O errors.
- **Warnings:** may warn for permissive recoveries (see §6).

#### `assert_toon(x, *, is_file = FALSE, strict = TRUE, allow_comments = TRUE, allow_duplicate_keys = TRUE)`
- Calls validator and **throws** `toonlite_parse_error` if invalid (includes attached location/snippet).

#### `format_toon(x, *, is_file = FALSE, indent = 2L, canonical = FALSE, allow_comments = TRUE)`
- **Purpose:** Pretty-print/reformat TOON without changing meaning.
- **canonical=TRUE:** stable representation:
  - stable key ordering (lexicographic) **optional**; default OFF because ordering may be semantically/UX-relevant.
  - stable quoting/delimiters.
- If `is_file=TRUE`, returns formatted text (does not rewrite file unless future `format_toon_file()` is added).

### 2.4 Tabular lane (fast data.frame I/O)

#### `read_toon_df(file, *, key = NULL, strict = TRUE, allow_comments = TRUE, allow_duplicate_keys = TRUE, warn = TRUE, col_types = NULL, ragged_rows = c("expand_warn","error"), n_mismatch = c("warn","error"), max_extra_cols = Inf)`
- **Reads** a TOON **tabular array** into a base `data.frame`.
- **key:** if non-NULL, extract tabular array at `root[key]` (root must be object).
- **col_types:** optional named character vector specifying column types: `"logical"|"integer"|"double"|"character"`.

**Ragged row policy (`ragged_rows`):**
- `"expand_warn"` (default, noob-friendly):
  - If a row has **fewer** fields than current schema → fill with NULL/NA (typed NA) to match schema.
  - If a row has **more** fields than schema → **expand schema** by adding new columns and backfilling earlier rows with NULL/NA.
  - Never drop “extras”.
  - Emit an aggregated **warning** describing min/max field counts and any schema expansion (unless `warn=FALSE`).
- `"error"` (power-user, predictable performance):
  - Any row with field count ≠ current schema triggers an error (strict rectangular table).

**Declared row count policy (`n_mismatch`):**
- `"warn"` (default): accept mismatch between declared `[N]` and observed rows, return observed rows, emit a single aggregated warning (unless `warn=FALSE`).
- `"error"` (power-user): require observed row count equals declared `[N]`; mismatch is an error.

**`max_extra_cols`:**
- Upper bound on how many new columns may be introduced by `"expand_warn"` ragged handling.
- If exceeded, error (protects against pathological inputs).

#### `write_toon_df(df, file, *, tabular = TRUE, pretty = TRUE, indent = 2L, strict = TRUE)`
- Writes `df` as a tabular TOON array by default.

#### `as_tabular_toon(x, *, strict = TRUE, warn = TRUE)`
- Attempts to convert an array-of-objects / list-of-records into a tabular representation suitable for `write_toon_df()`.
- In permissive mode:
  - union schema across records
  - missing fields → NULL
  - type promotion to accommodate all values.

### 2.5 Streaming (performance hero APIs)

#### `toon_stream_rows(file, *, key = NULL, callback, batch_size = 10000L, strict = TRUE, allow_comments = TRUE, allow_duplicate_keys = TRUE, warn = TRUE, col_types = NULL, ragged_rows = c("expand_warn","error"), n_mismatch = c("warn","error"), max_extra_cols = Inf)`
- Streams a tabular array in `batch_size` chunks as **data.frame** to `callback(df_batch)`.
- Callback return value ignored; exceptions propagate and abort parsing.
- Guarantees:
  - batches are independent and may be GC’d
  - stable column order within stream
- Row-shape (`ragged_rows`) and declared-row (`n_mismatch`) policies are identical to `read_toon_df()`; warnings may be emitted.

#### `toon_stream_items(file, *, key = NULL, callback, batch_size = 1000L, strict = TRUE, allow_comments = TRUE, allow_duplicate_keys = TRUE, warn = TRUE, simplify = TRUE)`
- Streams items from a non-tabular array (list batches).

#### `toon_stream_write_rows(file, *, schema, row_source, batch_size = 10000L, indent = 2L)`
- **schema:** character vector of column names + optional types.
- **row_source:** function that returns next batch as `data.frame` (or NULL to end).
- Writes a tabular TOON array without holding all rows in memory.

### 2.6 Conversions

#### JSON ↔ TOON (lossless lane)
- `json_to_toon(json, *, pretty = TRUE, strict = TRUE, allow_comments = TRUE)`
- `toon_to_json(toon, *, pretty = FALSE, strict = TRUE, allow_comments = TRUE)`

Implementation note:
- If `jsonlite` is installed, use it for JSON parsing/serialization.
- If not installed, provide a minimal internal JSON encoder/decoder **only** sufficient for the fixtures and common cases, or require `jsonlite` (recommended: require `jsonlite` for JSON conversions; keep it in Suggests and error with clear message if missing).

#### CSV ↔ TOON (tabular lane)
- `toon_to_csv(path_toon, path_csv, *, key = NULL, strict = TRUE, allow_comments = TRUE, warn = TRUE)`
- `csv_to_toon(path_csv, path_toon, *, tabular = TRUE, strict = TRUE, col_types = NULL)`

#### Parquet/Feather (optional; requires `arrow`)
- `toon_to_parquet(path_toon, path_parquet, *, key = NULL, strict = TRUE, allow_comments = TRUE, warn = TRUE)`
- `parquet_to_toon(path_parquet, path_toon, *, tabular = TRUE, strict = TRUE)`
- `toon_to_feather(path_toon, path_feather, *, key = NULL, strict = TRUE, allow_comments = TRUE, warn = TRUE)`
- `feather_to_toon(path_feather, path_toon, *, tabular = TRUE, strict = TRUE)`

Behavior:
- If `arrow` is missing, error: “Install arrow to use parquet/feather conversion.”

### 2.7 Introspection utilities

- `toon_peek(file, *, n = 50L, allow_comments = TRUE)`  
  Returns a small list describing top-level type, first keys, and first `n` lines (or parsed preview).
- `toon_info(file, *, allow_comments = TRUE)`  
  Returns structure summary: counts of arrays/objects, presence of tabular arrays, declared row counts.

---

## 3) Package structure (repo + R package)

### 3.1 Directory layout

```
toonlite/
  DESCRIPTION
  NAMESPACE
  R/
    api_core.R
    api_df.R
    api_stream.R
    api_convert.R
    api_validate.R
    conditions.R
    zzz.R
  src/
    toonlite.cpp          # .Call entry points
    toon_parser.hpp/.cpp  # lexer + parser core
    toon_encoder.hpp/.cpp # writer
    toon_df.hpp/.cpp      # tabular decoding/encoding
    toon_stream.hpp/.cpp  # streaming interfaces
    toon_io.hpp/.cpp      # buffered file reader
    toon_errors.hpp/.cpp  # error types + formatting
    init.c                # register .Call symbols
  inst/
    extdata/
      conformance/        # vendored fixtures or downloaded in CI
  tests/
    testthat/
      test-*.R
  tools/
    bench/                # benchmark scripts (optional)
```

### 3.2 Build settings

- C++ standard: **C++17**.
- Use R’s C API via `.Call` for performance and minimal dependencies.
- Register native routines (no dynamic symbol lookup).

---

## 4) TOON syntax support (implementation contract)

This section describes the subset/superset implemented. It is **not** a verbatim copy of the upstream TOON specification; it is the behavioral target for toonlite.

### 4.1 Data model

- **Object:** key/value pairs. Keys are strings (unquoted keys permitted only if allowed by spec; implement per spec).
- **Array:** ordered list of values.
- **Primitives:** `null`, booleans, numbers, strings.

### 4.2 Structural forms

Implement the following forms:

1. **Object field inline:** `key: value`
2. **Object field nested block:** `key:` followed by indented value block.
3. **Array header:** `[N]: ...` or `[N]:` then indented items.
4. **Array items (non-tabular):** lines starting with `- ` at correct indentation.
5. **Tabular array header:** `[N]{field1,field2,...}:` followed by row lines.
6. **Tabular rows:** delimited primitives, with delimiter determined by header or default.

### 4.3 Indentation rules

- Strict mode: **spaces only** for indentation; tab characters in leading indentation are parse errors.
- Indentation is measured as count of leading spaces; nesting requires strictly greater indentation than parent line.
- Mixed indentation levels are allowed, but nesting must be consistent.

### 4.4 Line endings

- Accept `\n` and `\r\n`. Treat `\r` alone as invalid in strict mode.

### 4.5 Comments (permissive extension; default ON)

Even if the base TOON spec does not define comments, toonlite supports a pragmatic extension:

- Line comments beginning with `#` or `//` after optional leading whitespace.
- Trailing comments are allowed if they occur after at least one whitespace and are not inside a string.

Controls:
- `allow_comments=TRUE` (default) enables this extension.
- In strict mode, this extension still applies unless user sets `allow_comments=FALSE`.

### 4.6 Duplicate keys in objects (permissive extension; default ON)

- If `allow_duplicate_keys=TRUE` (default), duplicate keys are accepted with **last-one-wins** semantics.
- Emit a warning (if `warn=TRUE` or in validator mode) summarizing duplicates: key name and count (bounded).

If `allow_duplicate_keys=FALSE`, duplicates are errors in strict mode.

---

## 5) R ↔ TOON type mapping rules

### 5.1 Parse-side mapping (TOON → R)

**General parse (`read_toon/from_toon`):**
- `null` → `NULL`
- boolean → `TRUE/FALSE`
- number:
  - if fits 32-bit signed integer and no decimal point → `integer`
  - else → `double`
- string → `character` (UTF-8)

**Arrays:**
- default: list
- if `simplify=TRUE` and all items are primitives of the same type (allowing NULL) → atomic vector
  - NULLs become `NA_*` of that vector type
  - if types mix, promote using: logical → integer → double → character
  - if any object/array exists, keep list

**Objects:**
- named list; if duplicate keys allowed, last wins.

### 5.2 Tabular parse mapping (TOON tabular array → data.frame)

- Columns are allocated as vectors of inferred type (or user-specified in `col_types`).
- `null` becomes typed `NA` for that column.
- Type inference ignores `null` values.
- Type promotion order: logical → integer → double → character.

### 5.3 Special numeric values

- On **write**: if `strict=TRUE`, any `NaN`, `Inf`, `-Inf` in numeric vectors triggers error.
- On **read**: only accept numeric grammar per spec; do not create NaN/Inf unless spec allows (assume not).

### 5.4 Dates/times normalization (write)

Default:
- `Date` → ISO string `YYYY-MM-DD`
- `POSIXct` → ISO 8601 `YYYY-MM-DDTHH:MM:SSZ` (UTC) unless attribute timezone preserved.
Options (future):
- `datetime = c("iso", "epoch")` with default `"iso"`.

---

## 6) Warnings and recoveries (per user decisions)

Some deviations are accepted but must be **warned** (unless user disables warnings).

### 6.1 Tabular row length mismatch (configurable)

Controlled by `ragged_rows`:

- `ragged_rows="expand_warn"` (default):
  - If row has fewer fields than schema: fill missing trailing fields with NULL/NA.
  - If row has more fields than schema: expand schema (subject to `max_extra_cols`) and backfill earlier rows with NULL/NA.
  - Warnings are aggregated (not one per row).
  - Example warning:
    - “Tabular rows had inconsistent field counts (min=3, max=7). Schema expanded to 7 columns; missing values filled with NA.”

- `ragged_rows="error"` (power-user):
  - Any row with field count ≠ schema is an error (no recovery, maximal predictability/performance).
### 6.2 Declared row count mismatch `[N]` (configurable)

Controlled by `n_mismatch`:

- `n_mismatch="warn"` (default):
  - If observed rows ≠ declared N: continue parsing, return observed rows, emit a single aggregated warning.
  - If observed > N: dynamically grow storage and shrink at end.
  - Example warning:
    - “Declared [N]=100000 but observed 99995 rows; using observed.”

- `n_mismatch="error"` (power-user):
  - Require observed row count equals declared `[N]`; mismatch is an error.
### 6.3 Duplicate keys (permissive; warn)

- last-one-wins, warn once per object or aggregated by file depending on parse mode.

### 6.4 Comments (permissive; no warning)

- Comments are ignored and do not warn by default.

---

## 7) Parsing architecture (C++ core)

### 7.1 Buffered I/O

No mmap. Implement a buffered reader:

- Read in chunks (e.g., 1–8 MB) into a buffer.
- Provide methods:
  - `bool next_line(StringView& out_line, size_t& out_line_no)`
  - handle CRLF normalization
- Avoid copying:
  - represent each line as `std::string_view` into the buffer when possible
  - if line spans buffer boundary, copy into a scratch string (rare; acceptable)

### 7.2 Lexer responsibilities (minimal)

TOON is line-oriented; keep “lexing” small:

- Count leading spaces → indentation level
- Strip trailing `\r`
- Identify comment-only lines (if `allow_comments`)
- For non-comment content:
  - classify by prefix patterns:
    - `- ` list item
    - contains `:` for key/value or header
    - starts with `[` for array header
  - parse primitives and delimited rows using targeted scanners (no regex)

### 7.3 Parser state machine (stack-based)

Maintain a stack of frames:

```
Frame {
  enum Kind { ROOT, OBJECT, ARRAY, TABULAR_ARRAY };
  int indent;                 // indentation level of the owning line
  // for OBJECT:
  ObjectBuilder* obj;
  // for ARRAY:
  ArrayBuilder* arr;
  // for TABULAR_ARRAY:
  TabularBuilder* tab;
}
```

Algorithm (high level):

1. Initialize ROOT frame at indent -1.
2. For each meaningful line:
   - compute indent `i`
   - while `i <= stack.top().indent` pop frames
   - parse line based on current top frame kind and line classification:
     - in OBJECT: parse `key: value` or `key:` (nested)
     - in ARRAY: parse `- value` or nested array header
     - in TABULAR_ARRAY: parse row line into tabular builder
     - at ROOT: allow object/array/primitive

3. When `key:` opens a nested value, push a new frame on next line when value is identified.

### 7.4 Primitive parsing

Implement fast parsing for:

- `null`, `true`, `false`
- number:
  - integer vs float detection
  - strict grammar per TOON spec (no locale, no exponent if disallowed)
  - use `std::from_chars` for integers; for doubles prefer `fast_float`-style algorithm, or `from_chars` if available and fast enough.
- string:
  - quoted with `"`
  - escapes: `\\`, `\"`, `\n`, `\r`, `\t` (strict)
  - reject invalid escape sequences in strict mode
  - validate UTF-8 in strict mode (optional, but recommended)

### 7.5 Header parsing (arrays + tabular)

Parse array headers without regex:

- optional key prefix (`key` + maybe no quotes)
- `[N]` mandatory for arrays in TOON canonical form
- optional `{fields...}` indicates tabular array (df lane)
- delimiter config (if spec allows explicit delimiter tokens)
- trailing `:`

Store:
- declared N (size_t)
- fields vector (optional)
- delimiter (char or enum)
- “tabular” flag

### 7.6 TabularBuilder design

Goal: fill R vectors efficiently.

Maintain a dynamic schema:

- `std::vector<std::string> col_names`
- `std::vector<ColBuilder> cols`
- `size_t declared_rows; size_t capacity_rows; size_t n_rows_seen;`

ColBuilder supports type promotion:

```
enum ColType { LOGICAL, INTEGER, DOUBLE, STRING };

struct ColBuilder {
  ColType type;
  // storage as R vectors created at finalize, or as C++ buffers then copied
  std::vector<int>   lgl;   // 0/1/NA_LOGICAL
  std::vector<int>   i32;   // NA_INTEGER
  std::vector<double> dbl;  // NA_REAL
  std::vector<std::string> str; // store as std::string; convert to STRSXP at finalize
  void ensure_rows(size_t n);
  void set(size_t row, Value v); // handles promotion
}
```

Promotion rules:
- LOGICAL + INTEGER → INTEGER
- INTEGER + DOUBLE → DOUBLE
- any + STRING → STRING
- null values set NA for current type.

When schema expands (row longer than schema):
- append new ColBuilders initialized to inferred type or default STRING?
- set earlier rows to NA (just ensure capacity and leave default NA)

Finalization:
- allocate R vectors of final type and length `n_rows_seen`
- fill from buffers; for strings, create STRSXP and set elements.

### 7.7 General DOM builders (non-tabular)

Represent parsed structure as a lightweight variant tree:

```
struct Node {
  enum Kind { N_NULL, N_BOOL, N_INT, N_DOUBLE, N_STRING, N_ARRAY, N_OBJECT };
  Kind kind;
  // store value or children
}
```

Then convert to SEXP at end:
- OBJECT → VECSXP + names
- ARRAY → VECSXP or atomic if simplify requested
- primitives → scalar vectors or NULL

For performance:
- prefer building directly into SEXP in streaming situations only; for DOM parse, a C++ tree is acceptable but watch allocations.

---

## 8) Encoder architecture (R → TOON)

### 8.1 Writer buffer

Use a growable buffer (`std::string` with reserve) or `std::vector<char>`.

Provide:
- `append(const char*, size_t)`
- `append_char(char)`
- `append_escaped_string(...)`

### 8.2 Selecting representation

Rules:

- `data.frame` in `write_toon()`:
  - by default encode as tabular array (`[N]{fields}:` + rows)
  - allow override `tabular=FALSE` only in df-specific writer if added later

- Named list → object (`key: ...`)
- Unnamed list → array (`[N]:` + either inline or `- ` style)
- Atomic vector length > 1 → array `[N]: ...`
- Scalars → primitive

### 8.3 Tabular encoding details

- Column names = `names(df)`
- N = `nrow(df)`
- Default delimiter: comma
- Row encoding:
  - logical → `true/false/null`
  - integer/double → numeric text (strict; reject NaN/Inf)
  - character → quoted strings with escapes
- Missing values:
  - `NA` → `null`

---

## 9) R wrappers and `.Call` interface

### 9.1 Native entry points (suggested)

Expose a small set of `.Call` functions:

- `C_from_toon(text, strict, simplify, allow_comments, allow_duplicate_keys) -> SEXP`
- `C_to_toon(x, pretty, indent, strict) -> CHARSXP/STRSXP`
- `C_validate_toon(text_or_file, is_file, strict, allow_comments, allow_duplicate_keys) -> logical with attributes? (or a list)`
- `C_read_toon_df(file, key, strict, allow_comments, allow_duplicate_keys, warn, col_types) -> data.frame`
- `C_stream_rows(file, key, batch_size, ...) -> list of batches?`  
  **Preferred:** push batches into R callback from C++ cautiously (see below).

### 9.2 Streaming callback mechanics

Calling back into R from C++ is allowed but must be done carefully:

- Resolve callback SEXP once.
- Use `R_tryEval` to call callback with batch.
- Check for user interrupt periodically (e.g., every N lines) using `R_CheckUserInterrupt()`.

Alternative: implement streaming as an iterator object with `next_batch()` to avoid callback overhead. For v1, keep callback but implement batched calls to amortize overhead.

### 9.3 Conditions, warnings, and structured errors

Define R condition constructors in `R/conditions.R`:

- `new_toonlite_parse_error(message, line, column, snippet, file)`
- warnings: `warning(condition)` where condition is class `toonlite_warning`

Validator returns FALSE with `attr(error)=...` instead of throwing.

---

## 10) Testing strategy

### 10.1 Unit tests

- Primitive parsing: booleans, null, numbers, strings, escapes.
- Indentation and nesting.
- Arrays: inline, list-item, nested.
- Objects: inline and nested.
- Duplicate keys behavior (warn + last-wins).
- Comments behavior (allowed by default).

### 10.2 Tabular tests

- Exact decoding of `[N]{fields}` arrays.
- Type inference and promotion.
- Row length mismatch: short rows filled; long rows expand schema; warnings aggregated.
- Row count mismatch: accept and warn, return observed.

### 10.3 Round-trip tests

- `x -> to_toon -> from_toon` equals normalized `x` (within mapping rules).
- `df -> write_toon_df -> read_toon_df` equals df (taking NA mapping into account).

### 10.4 Conformance fixtures (required)

- Vendor or fetch official TOON fixtures in CI.
- Run:
  - decode success tests
  - encode tests (JSON→TOON canonicalization)
  - strict-mode error tests

If fixtures include features toonlite intentionally extends (comments), ensure tests run with `allow_comments=FALSE` where required.

### 10.5 Fuzzing (optional but recommended)

- Randomly generated TOON-like inputs; ensure parser does not crash and errors are well-formed.
- Use sanitizers in a non-CRAN CI job (ASAN/UBSAN) if feasible.

---

## 11) Benchmarks and performance targets

### 11.1 Bench harness (not on CRAN checks)

Provide scripts in `tools/bench/`:

- Generate tabular TOON files with:
  - 1e6, 1e7 rows
  - varying column types and widths
- Compare:
  - `read_toon_df()` throughput (rows/sec)
  - memory peak
  - streaming throughput with `toon_stream_rows()`

### 11.2 Targets (rough, aspirational)

On a modern laptop:
- `read_toon_df()` should be competitive with `data.table::fread` on similar text sizes (within a constant factor).
- Streaming should keep memory bounded by:
  - O(batch_size * ncol) + parser buffers.

---

## 12) Documentation requirements

- README:
  - what TOON is (one paragraph)
  - “when to use toonlite” (LLM payloads, structured logs, tabular arrays)
  - quick examples
- Vignettes (optional):
  - “Large tabular files with streaming”
  - “TOON ↔ JSON conversions”

---

## 13) Versioning and compatibility

- Start at `0.1.0` for initial CRAN release.
- Guarantee:
  - stable API names for v1
  - stable formatting for `canonical=TRUE`
- Any changes to permissive defaults must be a minor-version bump at minimum.

---

## 14) Explicit micro-decisions captured (for the agent)

- v1 scope = full suite as listed.
- strict-by-default for core grammar, but **allow_comments=TRUE** and **allow_duplicate_keys=TRUE** by default.
- numbers parse to integer/double only (no `bit64` dependency).
- special floats rejected in strict mode.
- ragged rows: default `ragged_rows="expand_warn"` (fill missing + expand schema, warn aggregated); power-user `ragged_rows="error"`.
- row count mismatch: default `n_mismatch="warn"`; power-user `n_mismatch="error"`.
- indentation: spaces only in strict.
- CRLF accepted.
- no mmap.
- `.Call` + C++17.
- validation API returns boolean; provide `assert_toon()` for throwing behavior.
- package name: `toonlite`
- license: MIT
