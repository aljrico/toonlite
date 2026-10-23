# toonlite <img src="man/figures/logo.svg" align="right" height="139" alt="toonlite logo" />

<!-- badges: start -->
[![CRAN status](https://www.r-pkg.org/badges/version/toonlite)](https://cran.r-project.org/package=toonlite)
[![R-CMD-check](https://github.com/aljrico/toonlite/actions/workflows/R-CMD-check.yaml/badge.svg)](https://github.com/aljrico/toonlite/actions/workflows/R-CMD-check.yaml)
<!-- badges: end -->

Read, write, validate, stream and convert [TOON](https://toonformat.dev/), a
text format for feeding structured data to LLMs, in R. C++ backend, no
required dependencies.

## Why TOON?

TOON nests like JSON, but an array of same-shaped records is written as a
table: the field names once, then one row per record. For example:

```
results[3]{id,user{name,country},plan}:
  1,Ada Ng,ES,free
  2,Eli Smith,DE,pro
  3,Ada Ruiz,US,free
```

That is where it pays off, and only there. Tokens (o200k_base) for 200
records, from [`bench/shapes.R`](bench/shapes.R):

| Data | TOON | JSON (compact) | TOON vs JSON |
| --- | --- | --- | --- |
| API response, flat records | 3,712 | 6,388 | -42% |
| API response, records with a nested object | 3,100 | 5,577 | -44% |
| Records with variable-length arrays | 4,114 | 3,171 | +30% |
| Small config object | 61 | 52 | +17% |

Use TOON when your data is JSON-shaped and mostly arrays of uniform records:
API responses, query results, event logs. That is a lot of what goes into a
prompt. If the data is one flat table, CSV is about 10% smaller still
([results](bench/results.md#tokens-1000-rows--items)). If the records vary in
shape, or it is a small config, stay with JSON.

Because arrays declare their length and fields (`[3]{...}`), a truncated
document is caught when it is read back, not silently accepted.

## Why toonlite?

* **Correct.** Implements [TOON spec v4.4.0](https://github.com/toon-format/spec)
  and passes all 564 official conformance fixtures, so it reads what other
  TOON implementations write and the other way round.
* **Reads and writes.** The other TOON package on CRAN, `toon`, only writes.
  It errors on empty vectors, and its nested output is not indented, so it
  is not valid TOON.
* **Fast.** A 1,000,000-row data frame writes in 0.2 s and reads in 0.3 s:
  about 10x jsonlite and 500x `toon`. data.table's CSV is about 3x faster
  still, but CSV cannot hold nested data. See [Benchmarks](#benchmarks).
* **Exact.** Doubles read back bit for bit.
* **Streams.** `toon_stream_rows()` and `toon_stream_items()` read large
  files in batches with flat memory.
* **Light.** No required dependencies. Converts to and from CSV, and, with
  jsonlite or arrow installed, JSON, Parquet and Feather.

## Installation

```r
# Install from CRAN
install.packages("toonlite")

# Or the development version from GitHub
# install.packages("pak")
pak::pak("aljrico/toonlite")
```

## Quick start

```r
library(toonlite)

# Parse TOON
data <- from_toon("
name: Alice
age: 30
tags[2]: admin,dev
")
str(data)
#> List of 3
#>  $ name: chr "Alice"
#>  $ age : int 30
#>  $ tags: chr [1:2] "admin" "dev"

# Serialize to TOON
to_toon(list(x = 1, y = c(2.5, 3)))
#> x: 1
#> y[2]: 2.5,3

# Validate
validate_toon("tags[3]: a,b")
#> [1] FALSE
#> attr(,"error")$message
#> [1] "Declared 3 values but found 2"
#> ...

# File I/O
path <- tempfile(fileext = ".toon")
write_toon(data, path)
data <- read_toon(path)
```

R values map to TOON as follows: named lists are objects, unnamed lists and
vectors of length other than 1 are arrays, length-1 vectors are primitives
(wrap in `I()` for a one-element array), data frames are tabular arrays, and
`NA`, `NaN` and `Inf` are `null`. Decoding returns named lists for objects
and, with `simplify = TRUE`, atomic vectors for arrays of one primitive type.

Full syntax: [toonformat.dev](https://toonformat.dev/reference/spec.html).

## Data frames and streaming

```r
# Write a data frame as a tabular array, and read it back
path <- tempfile(fileext = ".toon")
write_toon_df(mtcars[1:3, 1:4], path)
df <- read_toon_df(path)

# Stream large files in batches of rows (data frames)...
toon_stream_rows("large.toon", batch_size = 10000,
  callback = function(batch) {
    # Process each batch
  }
)

# ...or the items of any array (lists)
toon_stream_items("events.toon", key = "events",
  callback = function(items) {
    # Process each batch
  }
)
```

## Format conversion

```r
# JSON (requires jsonlite)
toon <- json_to_toon('{"x": 1}')
json <- toon_to_json("x: 1")

# CSV
csv_to_toon("data.csv", "data.toon")
toon_to_csv("data.toon", "data.csv")

# Parquet (requires arrow)
toon_to_parquet("data.toon", "data.parquet")
```

## Benchmarks

Apple M4 Pro, 48 GB RAM, macOS 27, R 4.5.1, 23 October 2026. toonlite
0.2.0, toon 0.0.1 (the other TOON package on CRAN), jsonlite 2.0.0,
data.table 1.17.8. Medians from `bench::mark()`; peak memory is resident
set size above a fresh R session. Script and full results:
[`bench/`](bench/results.md).

A 1,000,000-row data frame (int, double, character and logical columns,
5% NA):

| Format | Write | Read | Read peak mem | File | Reads back exactly |
| --- | --- | --- | --- | --- | --- |
| toonlite | 196 ms | 300 ms | 115 MB | 51.3 MB | yes |
| toon | 95 s | no reader | - | 51.5 MB | doubles to 15 digits |
| jsonlite | 2.60 s | 3.21 s | 1617 MB | 92.4 MB | doubles to 15 digits |
| data.table (`fwrite`/`fread`) | 60 ms | 74 ms | 96 MB | 47.2 MB | doubles to 15 digits |
| base (`write.csv`/`read.csv`) | 1.54 s | 1.55 s | 299 MB | 49.2 MB | doubles to 15 digits |

<img src="man/figures/bench-tabular.png" alt="Write and read times for a 1,000,000-row data frame" width="640" />

100,000 nested records (`{id, user: {name, age}, tags: [0-3 strings], score}`):

| Format | Write | Read | File |
| --- | --- | --- | --- |
| toonlite | 79 ms | 105 ms | 9.8 MB |
| jsonlite | 13.2 s | 143 ms | 8.4 MB |

toon 0.0.1 cannot write these: it errors on the empty tag vectors, and
without them its output is not valid TOON.

Streaming cuts peak memory by more than half at the same speed:
`toon_stream_rows()` reads the 1,000,000-row file in batches of 10,000 with
a 48 MB peak, and `toon_stream_items()` reads the 100,000 records with a
30 MB peak (167 MB for `read_toon()`).

## Spec compatibility

Passes 564/564 official conformance fixtures (404 decode, 160 encode).
Where R differs:

* `\u0000` (NUL) cannot be stored in R strings, so decoding it is an error.
* Integers are R integers between -2147483647 and 2147483647 and doubles
  otherwise; integers beyond 2^53 lose precision, numbers beyond the double
  range become `-Inf`/`Inf`.
* `read_toon_df(ragged_rows = "expand_warn", n_mismatch = "warn")` are opt-in
  extensions that accept input the spec rejects.

Limits: encoding and decoding stop with an error beyond 1000 levels of
nesting.

## License

MIT
