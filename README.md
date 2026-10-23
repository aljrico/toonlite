# toonlite <img src="man/figures/logo.svg" align="right" height="139" alt="toonlite logo" />

<!-- badges: start -->
[![CRAN status](https://www.r-pkg.org/badges/version/toonlite)](https://cran.r-project.org/package=toonlite)
[![R-CMD-check](https://github.com/aljrico/toonlite/actions/workflows/R-CMD-check.yaml/badge.svg)](https://github.com/aljrico/toonlite/actions/workflows/R-CMD-check.yaml)
<!-- badges: end -->

Read, write, validate, stream and convert [TOON](https://toonformat.dev/)
data in R, with a C++ backend and no required dependencies.

TOON is a compact, human-readable format for passing structured data to LLMs.
Like CSV, a uniform array declares its fields once; like JSON, it nests.
Whether that saves tokens depends on the shape of the data: see
[Benchmarks](#benchmarks).

## Installation

```r
# Install from CRAN
install.packages("toonlite")

# Or install the development version from GitHub
# install.packages("devtools")
devtools::install_github("aljrico/toonlite")
```

## Spec compatibility

Implements [TOON spec v4.4.0](https://github.com/toon-format/spec) and passes
564/564 official conformance fixtures (404 decode, 160 encode). Known
deviations, all from R's data model:

* `\u0000` (NUL) cannot be stored in R strings, so decoding it is an error.
* Integers are R integers between -2147483647 and 2147483647 and doubles
  otherwise; integers beyond 2^53 lose precision, numbers beyond the double
  range become `-Inf`/`Inf`.
* Encoding and decoding stop with an error beyond 1000 levels of nesting.
* `read_toon_df(ragged_rows = "expand_warn", n_mismatch = "warn")` are opt-in
  extensions that accept input the spec rejects.

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

# File I/O
write_toon(data, "config.toon")
data <- read_toon("config.toon")
```

R values map to TOON as follows: named lists are objects, unnamed lists and
vectors of length other than 1 are arrays, length-1 vectors are primitives
(wrap in `I()` for a one-element array), data frames are tabular arrays, and
`NA`, `NaN` and `Inf` are `null`. Decoding returns named lists for objects
and, with `simplify = TRUE`, atomic vectors for arrays of one primitive type.

## Tabular data

TOON's tabular syntax declares the fields once:

```
users[3]{name,age,score}:
  Alice,30,95.5
  Bob,25,87
  Charlie,35,92.3
```

```r
# Write a data.frame
write_toon_df(mtcars[1:3, 1:4], "cars.toon")

# Read it back
df <- read_toon_df("cars.toon")

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

## The format

**Objects** are key-value pairs; nesting is by indentation:
```
name: Alice
address:
  city: Paris
```

**Arrays** declare their length. Primitives go inline, anything else is a
list of `- ` items:
```
colors[3]: red,green,blue
mixed[3]:
  - 1
  - text
  - id: 7
    ok: true
```

**Tabular** arrays of uniform objects use a header with fields, then rows:
```
[3]{name,age}:
  Alice,30
  Bob,25
  Charlie,35
```

**Types**: strings (quoted only when needed), numbers, booleans, and null:
```
name: Alice
zip: "01234"
score: 95.5
active: true
nickname: null
```

**Comments** are whole lines starting with `#`:
```
# connection settings
server: localhost
port: 8080
```

For the full format specification, see [toonformat.dev](https://toonformat.dev/reference/spec.html).

## Benchmarks

Apple M4 Pro, 48 GB RAM, macOS 27, R 4.5.1, 23 October 2026. toonlite
0.2.0, toon 0.0.1 (the other TOON package on CRAN), jsonlite 2.0.0,
data.table 1.17.8. Medians from `bench::mark()`; peak memory is resident
set size above a fresh R session. Script and full results:
[`bench/`](https://github.com/aljrico/toonlite/blob/main/bench/results.md).

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
without them its output does not decode to the input.

Tokens (o200k_base) for 1,000 rows or records:

| Data | TOON | JSON (compact) | CSV |
| --- | --- | --- | --- |
| Table | 22,240 | 32,354 | 20,155 |
| Nested records | 38,079 | 27,673 | - |

TOON beats JSON on tables and loses to CSV. On nested records that are not
uniform, the indentation costs more than JSON's quotes and braces.

Streaming keeps memory flat: `toon_stream_rows()` reads the 1,000,000-row
file in batches of 10,000 with a 48 MB peak (131 MB for `read_toon_df()`)
in the same time, and `toon_stream_items()` reads the 100,000 records with
a 30 MB peak (167 MB for `read_toon()`).

## License

MIT
