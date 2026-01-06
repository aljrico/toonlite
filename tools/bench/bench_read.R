#!/usr/bin/env Rscript
# Benchmark read performance
#
# Usage: Rscript bench_read.R [data_dir] [iterations]

library(toonlite)

args <- commandArgs(trailingOnly = TRUE)
data_dir <- if (length(args) > 0) args[1] else "bench_data"
iterations <- if (length(args) > 1) as.integer(args[2]) else 3

if (!dir.exists(data_dir)) {
  stop("Data directory not found: ", data_dir,
       "\nRun generate_fixtures.R first.")
}

cat("=== toonlite Read Benchmarks ===\n")
cat("Data directory:", data_dir, "\n")
cat("Iterations:", iterations, "\n\n")

# Benchmark function
bench_read <- function(filepath, read_fn, name) {
  if (!file.exists(filepath)) {
    cat(sprintf("  %-40s SKIP (file not found)\n", name))
    return(NULL)
  }

  size_mb <- file.info(filepath)$size / 1024 / 1024

  # Warmup
  invisible(read_fn(filepath))

  # Timed runs
  times <- numeric(iterations)
  for (i in seq_len(iterations)) {
    gc(verbose = FALSE)
    start <- Sys.time()
    result <- read_fn(filepath)
    times[i] <- as.numeric(Sys.time() - start, units = "secs")
  }

  median_time <- median(times)
  throughput <- size_mb / median_time

  cat(sprintf("  %-40s %6.2f MB  %6.3f sec  %7.2f MB/s\n",
              name, size_mb, median_time, throughput))

  list(
    name = name,
    size_mb = size_mb,
    median_sec = median_time,
    throughput_mbps = throughput
  )
}

# Tabular benchmarks
cat("Tabular read (read_toon_df):\n")
tabular_files <- list.files(data_dir, pattern = "^tabular_.*\\.toon$",
                             full.names = TRUE)
tabular_results <- lapply(tabular_files, function(f) {
  bench_read(f, read_toon_df, basename(f))
})

# General read benchmarks
cat("\nGeneral read (read_toon):\n")
nested_files <- list.files(data_dir, pattern = "^nested_.*\\.toon$",
                            full.names = TRUE)
nested_results <- lapply(nested_files, function(f) {
  bench_read(f, read_toon, basename(f))
})

array_files <- list.files(data_dir, pattern = "^array_.*\\.toon$",
                           full.names = TRUE)
array_results <- lapply(array_files, function(f) {
  bench_read(f, read_toon, basename(f))
})

# Streaming benchmark
cat("\nStreaming read (toon_stream_rows):\n")
for (f in tabular_files) {
  if (!file.exists(f)) next

  size_mb <- file.info(f)$size / 1024 / 1024
  row_count <- 0

  gc(verbose = FALSE)
  start <- Sys.time()
  toon_stream_rows(f, callback = function(batch) {
    row_count <<- row_count + nrow(batch)
  }, batch_size = 10000L)
  elapsed <- as.numeric(Sys.time() - start, units = "secs")

  throughput <- size_mb / elapsed
  cat(sprintf("  %-40s %6.2f MB  %6.3f sec  %7.2f MB/s  (%d rows)\n",
              basename(f), size_mb, elapsed, throughput, row_count))
}

cat("\nDone!\n")
