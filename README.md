# toonlite <img src="man/figures/logo.svg" align="right" height="139" alt="toonlite logo" />

<!-- badges: start -->
[![CRAN status](https://www.r-pkg.org/badges/version/toonlite)](https://cran.r-project.org/package=toonlite)
[![R-CMD-check](https://github.com/aljrico/toonlite/actions/workflows/R-CMD-check.yaml/badge.svg)](https://github.com/aljrico/toonlite/actions/workflows/R-CMD-check.yaml)
<!-- badges: end -->

**Fast, minimal-dependency R interface for [TOON](https://toonformat.dev/) data** — read, write, validate, stream, and convert with a C++ backend.

TOON is a compact, human-readable serialization format designed for passing structured data to LLMs with significantly reduced token usage.

## Why TOON?

JSON is verbose. YAML is ambiguous. CSV can't nest. TOON gives you readability, explicit types, nested structures, and comments, with significantly fewer tokens.

Based on the [TOON benchmark](https://github.com/toon-format/toon) (209 retrieval questions across 4 LLM models):

<table>
<tr>
<td>

**Token usage** (lower = better)
```
TOON  ▓▓▓▓▓▓▓▓▓▓          2,744
YAML  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓      3,719  (+36%)
JSON  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓   4,545  (+66%)
```

</td>
<td>

**Retrieval accuracy** (higher = better)
```
TOON  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  73.9%
JSON  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓   69.7%
YAML  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓   69.0%
```

</td>
</tr>
</table>

TOON is particularly useful for LLM payloads, structured logs, and large tabular datasets where you want both readability and performance.

## Installation

```r
# Install from CRAN
install.packages("toonlite")

# Or install the development version from GitHub
# install.packages("devtools")
devtools::install_github("aljrico/toonlite")
```

## Quick start

```r
library(toonlite)

# Parse TOON
data <- from_toon('
name: "Alice"
age: 30
active: true
')
#> list(name = "Alice", age = 30L, active = TRUE)

# Serialize to TOON
to_toon(list(x = 1, y = 2))
#> x: 1.0
#> y: 2.0

# File I/O
write_toon(data, "config.toon")
data <- read_toon("config.toon")
```

## Tabular data

TOON's tabular syntax is compact and fast to parse:

```
# 3 is the row count; {name, age, score} declares the columns
[3]{name, age, score}:
  "Alice", 30, 95.5
  "Bob", 25, 87.0
  "Charlie", 35, 92.3
```

```r
# Write a data.frame
write_toon_df(mtcars[1:3, 1:4], "cars.toon")

# Read it back
df <- read_toon_df("cars.toon")

# Stream large files without loading into memory
toon_stream_rows("large.toon", batch_size = 10000,
  callback = function(batch) {
    # Process each batch
  }
)
```

## Format conversion

```r
# JSON (requires jsonlite)
toon <- json_to_toon('{"x": 1}')
json <- toon_to_json('x: 1')

# CSV
csv_to_toon("data.csv", "data.toon")
toon_to_csv("data.toon", "data.csv")

# Parquet (requires arrow)
toon_to_parquet("data.toon", "data.parquet")
```

## The format

**Objects** are key-value pairs:
```
name: "Alice"
age: 30
```

**Arrays** use a bracketed count with `- ` items:
```
colors: [3]:
  - "red"
  - "green"
  - "blue"
```

**Tabular** uses a header with fields, then rows:
```
[3]{name, age}:
  "Alice", 30
  "Bob", 25
  "Charlie", 35
```

**Types** (strings, integers, doubles, booleans, and null):
```
name: "Alice"
age: 30
score: 95.5
active: true
nickname: null
```

**Comments** use `#` or `//`:
```
server: "localhost"  # default
port: 8080           // can override
```

For the full format specification, see [toonformat.dev](https://toonformat.dev/reference/spec.html).

## License

MIT
