# write_dataset checks for format-specific arguments

    Code
      write_dataset(df, dst_dir, format = "feather", compression = "snappy")
    Error <rlang_error>
      `compression` is not a valid argument for your chosen `format`.
      i You could try using `codec` instead of `compression`.
      i Supported arguments: `use_legacy_format`, `metadata_version`, `codec`, and `null_fallback`.

---

    Code
      write_dataset(df, dst_dir, format = "feather", nonsensical_arg = "blah-blah")
    Error <rlang_error>
      `nonsensical_arg` is not a valid argument for your chosen `format`.
      i Supported arguments: `use_legacy_format`, `metadata_version`, `codec`, and `null_fallback`.

---

    Code
      write_dataset(df, dst_dir, format = "arrow", nonsensical_arg = "blah-blah")
    Error <rlang_error>
      `nonsensical_arg` is not a valid argument for your chosen `format`.
      i Supported arguments: `use_legacy_format`, `metadata_version`, `codec`, and `null_fallback`.

---

    Code
      write_dataset(df, dst_dir, format = "ipc", nonsensical_arg = "blah-blah")
    Error <rlang_error>
      `nonsensical_arg` is not a valid argument for your chosen `format`.
      i Supported arguments: `use_legacy_format`, `metadata_version`, `codec`, and `null_fallback`.

---

    Code
      write_dataset(df, dst_dir, format = "csv", nonsensical_arg = "blah-blah")
    Error <rlang_error>
      `nonsensical_arg` is not a valid argument for your chosen `format`.
      i Supported arguments: `include_header` and `batch_size`.

---

    Code
      write_dataset(df, dst_dir, format = "parquet", nonsensical_arg = "blah-blah")
    Error <rlang_error>
      `nonsensical_arg` is not a valid argument for your chosen `format`.
      i Supported arguments: `chunk_size`, `version`, `compression`, `compression_level`, `use_dictionary`, `write_statistics`, `data_page_size`, `use_deprecated_int96_timestamps`, `coerce_timestamps`, `allow_truncated_timestamps`, `properties`, and `arrow_properties`.

