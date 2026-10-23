# Token counts for TOON vs JSON across common data shapes.
#
#   Rscript bench/shapes.R
#
# Needs jsonlite and uv (Python tiktoken, o200k_base). Prints a markdown table.

library(toonlite)
library(jsonlite)
set.seed(42)

n <- 200
names <- paste(sample(c("Ada", "Bo", "Cleo", "Dev", "Eli", "Fay"), n, TRUE),
               sample(c("Smith", "Ng", "Ruiz", "Okafor"), n, TRUE))
plan <- sample(c("free", "pro", "team"), n, TRUE)
spend <- round(runif(n, 0, 500), 2)

shapes <- list(
  "API response, flat records" = list(
    page = 1L, total = 1234L, next_cursor = "abc123",
    results = data.frame(id = 1:n, name = names, email = paste0("user", 1:n, "@example.com"),
                         plan = plan, active = sample(c(TRUE, FALSE), n, TRUE), spend = spend)
  ),
  "API response, records with a nested object" = list(
    page = 1L,
    results = lapply(1:n, function(i) list(
      id = i, user = list(name = names[i], country = sample(c("ES", "US", "DE"), 1)),
      plan = plan[i], spend = spend[i]
    ))
  ),
  "Records with variable-length arrays" = list(
    page = 1L,
    results = lapply(1:n, function(i) list(
      id = i, name = names[i], tags = I(sample(c("a", "b", "c", "d"), sample(0:3, 1)))
    ))
  ),
  "Small config object" = list(
    service = "billing", version = "2.3.1",
    db = list(host = "db.internal", port = 5432L, pool = 10L),
    features = c("sso", "audit", "export"),
    retries = list(max = 5L, backoff_ms = 200L)
  )
)

dir <- tempfile("shapes")
dir.create(dir)
paths <- character()
for (i in seq_along(shapes)) {
  x <- shapes[[i]]
  p <- file.path(dir, paste0(i, c(".toon", ".json")))
  writeLines(to_toon(x), p[1], sep = "")
  writeLines(toJSON(x, auto_unbox = TRUE, dataframe = "rows", digits = NA), p[2], sep = "")
  paths <- c(paths, p)
}

py <- tempfile(fileext = ".py")
writeLines(c(
  "import sys, tiktoken",
  "enc = tiktoken.get_encoding('o200k_base')",
  "for p in sys.argv[1:]:",
  "    with open(p, encoding='utf-8') as fh:",
  "        print(len(enc.encode(fh.read())))"
), py)
tokens <- as.integer(system2("uv", c("run", "--quiet", "--with", "tiktoken", "python", "-I", py,
                                     shQuote(paths)), stdout = TRUE))
tokens <- matrix(tokens, ncol = 2, byrow = TRUE)

cat("| Data | TOON | JSON (compact) | TOON vs JSON |\n| --- | --- | --- | --- |\n")
for (i in seq_along(shapes)) {
  cat(sprintf("| %s | %s | %s | %+.0f%% |\n", names(shapes)[i],
              format(tokens[i, 1], big.mark = ","), format(tokens[i, 2], big.mark = ","),
              100 * (tokens[i, 1] / tokens[i, 2] - 1)))
}
