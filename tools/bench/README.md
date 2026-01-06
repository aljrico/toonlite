# toonlite Benchmarks

This directory contains benchmark scripts for measuring toonlite performance.

## Scripts

- `generate_fixtures.R` - Generate benchmark data files of various sizes
- `bench_read.R` - Benchmark read performance (read_toon, read_toon_df, streaming)
- `bench_write.R` - Benchmark write performance (write_toon, write_toon_df, to_toon)

## Quick Start

```bash
# Generate benchmark fixtures (creates bench_data/ directory)
Rscript tools/bench/generate_fixtures.R

# Run read benchmarks
Rscript tools/bench/bench_read.R

# Run write benchmarks
Rscript tools/bench/bench_write.R
```

## Custom Options

```bash
# Generate fixtures to custom directory
Rscript tools/bench/generate_fixtures.R /path/to/output

# Run benchmarks with more iterations
Rscript tools/bench/bench_read.R bench_data 5
Rscript tools/bench/bench_write.R 5
```

## Performance Targets

On a modern laptop, toonlite should achieve:

- **Tabular read**: Competitive with data.table::fread on similar text sizes
- **Streaming**: Memory bounded by O(batch_size * ncol) + parser buffers
- **Write**: Reasonable throughput for serialization

## Notes

- Benchmarks are NOT run during CRAN checks
- Results vary by hardware, file system, and data characteristics
- For meaningful comparisons, use the same hardware and multiple iterations
