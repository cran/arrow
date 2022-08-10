## -----------------------------------------------------------------------------
arrow::arrow_with_s3()

## ---- eval = FALSE------------------------------------------------------------
#  arrow::copy_files("s3://voltrondata-labs-datasets/nyc-taxi", "nyc-taxi")
#  # Alternatively, with GCS:
#  arrow::copy_files("gs://voltrondata-labs-datasets/nyc-taxi", "nyc-taxi")

## ---- eval = FALSE------------------------------------------------------------
#  bucket <- "https://voltrondata-labs-datasets.s3.us-east-2.amazonaws.com"
#  for (year in 2009:2019) {
#    if (year == 2019) {
#      # We only have through June 2019 there
#      months <- 1:6
#    } else {
#      months <- 1:12
#    }
#    for (month in sprintf("%02d", months)) {
#      dir.create(file.path("nyc-taxi", year, month), recursive = TRUE)
#      try(download.file(
#        paste(bucket, "nyc-taxi", paste0("year=", year), paste0("month=", month), "data.parquet", sep = "/"),
#        file.path("nyc-taxi", paste0("year=", year), paste0("month=", month), "data.parquet"),
#        mode = "wb"
#      ), silent = TRUE)
#    }
#  }

## -----------------------------------------------------------------------------
dir.exists("nyc-taxi")

## -----------------------------------------------------------------------------
library(arrow, warn.conflicts = FALSE)
library(dplyr, warn.conflicts = FALSE)

## ---- eval = file.exists("nyc-taxi")------------------------------------------
#  ds <- open_dataset("nyc-taxi")

## ---- eval = file.exists("nyc-taxi")------------------------------------------
#  ds

## ---- echo = FALSE, eval = !file.exists("nyc-taxi")---------------------------
cat("
FileSystemDataset with 158 Parquet files
vendor_name: string
pickup_datetime: timestamp[ms]
dropoff_datetime: timestamp[ms]
passenger_count: int64
trip_distance: double
pickup_longitude: double
pickup_latitude: double
rate_code: string
store_and_fwd: string
dropoff_longitude: double
dropoff_latitude: double
payment_type: string
fare_amount: double
extra: double
mta_tax: double
tip_amount: double
tolls_amount: double
total_amount: double
improvement_surcharge: double
congestion_surcharge: double
pickup_location_id: int64
dropoff_location_id: int64
year: int32
month: int32
")

## ---- eval = file.exists("nyc-taxi")------------------------------------------
#  system.time(ds %>%
#    filter(total_amount > 100, year == 2015) %>%
#    select(tip_amount, total_amount, passenger_count) %>%
#    mutate(tip_pct = 100 * tip_amount / total_amount) %>%
#    group_by(passenger_count) %>%
#    summarise(
#      median_tip_pct = median(tip_pct),
#      n = n()
#    ) %>%
#    collect() %>%
#    print())

## ---- echo = FALSE, eval = !file.exists("nyc-taxi")---------------------------
cat("
# A tibble: 10 x 3
   passenger_count median_tip_pct      n
             <int>          <dbl>  <int>
 1               1           16.6 143087
 2               2           16.2  34418
 3               5           16.7   5806
 4               4           11.4   4771
 5               6           16.7   3338
 6               3           14.6   8922
 7               0           10.1    380
 8               8           16.7     32
 9               9           16.7     42
10               7           16.7     11

   user  system elapsed
  4.436   1.012   1.402
")

## ---- eval = file.exists("nyc-taxi")------------------------------------------
#  ds %>%
#    filter(total_amount > 100, year == 2015) %>%
#    select(tip_amount, total_amount, passenger_count) %>%
#    mutate(tip_pct = 100 * tip_amount / total_amount) %>%
#    group_by(passenger_count) %>%
#    summarise(
#      median_tip_pct = median(tip_pct),
#      n = n()
#    )

## ---- echo = FALSE, eval = !file.exists("nyc-taxi")---------------------------
cat("
FileSystemDataset (query)
passenger_count: int64
median_tip_pct: double
n: int32

See $.data for the source Arrow object
")

## ---- eval = file.exists("nyc-taxi")------------------------------------------
#  sampled_data <- ds %>%
#    filter(year == 2015) %>%
#    select(tip_amount, total_amount, passenger_count) %>%
#    map_batches(~ as_record_batch(sample_frac(as.data.frame(.), 1e-4))) %>%
#    mutate(tip_pct = tip_amount / total_amount) %>%
#    collect()
#  
#  str(sampled_data)

## ---- echo = FALSE, eval = !file.exists("nyc-taxi")---------------------------
cat("
tibble [10,918 × 4] (S3: tbl_df/tbl/data.frame)
 $ tip_amount     : num [1:10918] 3 0 4 1 1 6 0 1.35 0 5.9 ...
 $ total_amount   : num [1:10918] 18.8 13.3 20.3 15.8 13.3 ...
 $ passenger_count: int [1:10918] 3 2 1 1 1 1 1 1 1 3 ...
 $ tip_pct        : num [1:10918] 0.1596 0 0.197 0.0633 0.0752 ...
")

## ---- eval = file.exists("nyc-taxi")------------------------------------------
#  model <- lm(tip_pct ~ total_amount + passenger_count, data = sampled_data)
#  
#  ds %>%
#    filter(year == 2015) %>%
#    select(tip_amount, total_amount, passenger_count) %>%
#    mutate(tip_pct = tip_amount / total_amount) %>%
#    map_batches(function(batch) {
#      batch %>%
#        as.data.frame() %>%
#        mutate(pred_tip_pct = predict(model, newdata = .)) %>%
#        filter(!is.nan(tip_pct)) %>%
#        summarize(sse_partial = sum((pred_tip_pct - tip_pct)^2), n_partial = n()) %>%
#        as_record_batch()
#    }) %>%
#    summarize(mse = sum(sse_partial) / sum(n_partial)) %>%
#    pull(mse)

## ---- echo = FALSE, eval = !file.exists("nyc-taxi")---------------------------
cat("
[1] 0.1304284
")

