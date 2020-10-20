// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include "./arrow_types.h"

#if defined(ARROW_R_WITH_ARROW)
#include <arrow/type.h>

// [[arrow::export]]
bool shared_ptr_is_null(SEXP xp) {
  return reinterpret_cast<std::shared_ptr<void>*>(R_ExternalPtrAddr(xp))->get() ==
         nullptr;
}

// [[arrow::export]]
bool unique_ptr_is_null(SEXP xp) {
  return reinterpret_cast<std::unique_ptr<void>*>(R_ExternalPtrAddr(xp))->get() ==
         nullptr;
}

// [[arrow::export]]
std::shared_ptr<arrow::DataType> Int8__initialize() { return arrow::int8(); }

// [[arrow::export]]
std::shared_ptr<arrow::DataType> Int16__initialize() { return arrow::int16(); }

// [[arrow::export]]
std::shared_ptr<arrow::DataType> Int32__initialize() { return arrow::int32(); }

// [[arrow::export]]
std::shared_ptr<arrow::DataType> Int64__initialize() { return arrow::int64(); }

// [[arrow::export]]
std::shared_ptr<arrow::DataType> UInt8__initialize() { return arrow::uint8(); }

// [[arrow::export]]
std::shared_ptr<arrow::DataType> UInt16__initialize() { return arrow::uint16(); }

// [[arrow::export]]
std::shared_ptr<arrow::DataType> UInt32__initialize() { return arrow::uint32(); }

// [[arrow::export]]
std::shared_ptr<arrow::DataType> UInt64__initialize() { return arrow::uint64(); }

// [[arrow::export]]
std::shared_ptr<arrow::DataType> Float16__initialize() { return arrow::float16(); }

// [[arrow::export]]
std::shared_ptr<arrow::DataType> Float32__initialize() { return arrow::float32(); }

// [[arrow::export]]
std::shared_ptr<arrow::DataType> Float64__initialize() { return arrow::float64(); }

// [[arrow::export]]
std::shared_ptr<arrow::DataType> Boolean__initialize() { return arrow::boolean(); }

// [[arrow::export]]
std::shared_ptr<arrow::DataType> Utf8__initialize() { return arrow::utf8(); }

// [[arrow::export]]
std::shared_ptr<arrow::DataType> LargeUtf8__initialize() { return arrow::large_utf8(); }

// [[arrow::export]]
std::shared_ptr<arrow::DataType> Binary__initialize() { return arrow::binary(); }

// [[arrow::export]]
std::shared_ptr<arrow::DataType> LargeBinary__initialize() {
  return arrow::large_binary();
}

// [[arrow::export]]
std::shared_ptr<arrow::DataType> Date32__initialize() { return arrow::date32(); }

// [[arrow::export]]
std::shared_ptr<arrow::DataType> Date64__initialize() { return arrow::date64(); }

// [[arrow::export]]
std::shared_ptr<arrow::DataType> Null__initialize() { return arrow::null(); }

// [[arrow::export]]
std::shared_ptr<arrow::DataType> Decimal128Type__initialize(int32_t precision,
                                                            int32_t scale) {
  // Use the builder that validates inputs
  return ValueOrStop(arrow::Decimal128Type::Make(precision, scale));
}

// [[arrow::export]]
std::shared_ptr<arrow::DataType> FixedSizeBinary__initialize(R_xlen_t byte_width) {
  if (byte_width == NA_INTEGER) {
    cpp11::stop("'byte_width' cannot be NA");
  }
  if (byte_width < 1) {
    cpp11::stop("'byte_width' must be > 0");
  }
  return arrow::fixed_size_binary(byte_width);
}

// [[arrow::export]]
std::shared_ptr<arrow::DataType> Timestamp__initialize(arrow::TimeUnit::type unit,
                                                       const std::string& timezone) {
  return arrow::timestamp(unit, timezone);
}

// [[arrow::export]]
std::shared_ptr<arrow::DataType> Time32__initialize(arrow::TimeUnit::type unit) {
  return arrow::time32(unit);
}

// [[arrow::export]]
std::shared_ptr<arrow::DataType> Time64__initialize(arrow::TimeUnit::type unit) {
  return arrow::time64(unit);
}

// [[arrow::export]]
SEXP list__(SEXP x) {
  if (Rf_inherits(x, "Field")) {
    auto field = cpp11::as_cpp<std::shared_ptr<arrow::Field>>(x);
    return cpp11::as_sexp(arrow::list(field));
  }

  if (Rf_inherits(x, "DataType")) {
    auto type = cpp11::as_cpp<std::shared_ptr<arrow::DataType>>(x);
    return cpp11::as_sexp(arrow::list(type));
  }

  cpp11::stop("incompatible");
  return R_NilValue;
}

// [[arrow::export]]
SEXP large_list__(SEXP x) {
  if (Rf_inherits(x, "Field")) {
    auto field = cpp11::as_cpp<std::shared_ptr<arrow::Field>>(x);
    return cpp11::as_sexp(arrow::large_list(field));
  }

  if (Rf_inherits(x, "DataType")) {
    auto type = cpp11::as_cpp<std::shared_ptr<arrow::DataType>>(x);
    return cpp11::as_sexp(arrow::large_list(type));
  }

  cpp11::stop("incompatible");
  return R_NilValue;
}

// [[arrow::export]]
SEXP fixed_size_list__(SEXP x, int list_size) {
  if (Rf_inherits(x, "Field")) {
    auto field = cpp11::as_cpp<std::shared_ptr<arrow::Field>>(x);
    return cpp11::as_sexp(arrow::fixed_size_list(field, list_size));
  }

  if (Rf_inherits(x, "DataType")) {
    auto type = cpp11::as_cpp<std::shared_ptr<arrow::DataType>>(x);
    return cpp11::as_sexp(arrow::fixed_size_list(type, list_size));
  }

  cpp11::stop("incompatible");
  return R_NilValue;
}

// [[arrow::export]]
std::shared_ptr<arrow::DataType> struct__(
    const std::vector<std::shared_ptr<arrow::Field>>& fields) {
  return arrow::struct_(fields);
}

// [[arrow::export]]
std::string DataType__ToString(const std::shared_ptr<arrow::DataType>& type) {
  return type->ToString();
}

// [[arrow::export]]
std::string DataType__name(const std::shared_ptr<arrow::DataType>& type) {
  return type->name();
}

// [[arrow::export]]
bool DataType__Equals(const std::shared_ptr<arrow::DataType>& lhs,
                      const std::shared_ptr<arrow::DataType>& rhs) {
  return lhs->Equals(*rhs);
}

// [[arrow::export]]
int DataType__num_children(const std::shared_ptr<arrow::DataType>& type) {
  return type->num_fields();
}

// [[arrow::export]]
cpp11::writable::list DataType__children_pointer(
    const std::shared_ptr<arrow::DataType>& type) {
  return arrow::r::to_r_list(type->fields());
}

// [[arrow::export]]
arrow::Type::type DataType__id(const std::shared_ptr<arrow::DataType>& type) {
  return type->id();
}

// [[arrow::export]]
std::string ListType__ToString(const std::shared_ptr<arrow::ListType>& type) {
  return type->ToString();
}

// [[arrow::export]]
int FixedWidthType__bit_width(const std::shared_ptr<arrow::FixedWidthType>& type) {
  return type->bit_width();
}

// [[arrow::export]]
arrow::DateUnit DateType__unit(const std::shared_ptr<arrow::DateType>& type) {
  return type->unit();
}

// [[arrow::export]]
arrow::TimeUnit::type TimeType__unit(const std::shared_ptr<arrow::TimeType>& type) {
  return type->unit();
}

// [[arrow::export]]
int32_t DecimalType__precision(const std::shared_ptr<arrow::DecimalType>& type) {
  return type->precision();
}

// [[arrow::export]]
int32_t DecimalType__scale(const std::shared_ptr<arrow::DecimalType>& type) {
  return type->scale();
}

// [[arrow::export]]
std::string TimestampType__timezone(const std::shared_ptr<arrow::TimestampType>& type) {
  return type->timezone();
}

// [[arrow::export]]
arrow::TimeUnit::type TimestampType__unit(
    const std::shared_ptr<arrow::TimestampType>& type) {
  return type->unit();
}

// [[arrow::export]]
std::shared_ptr<arrow::DataType> DictionaryType__initialize(
    const std::shared_ptr<arrow::DataType>& index_type,
    const std::shared_ptr<arrow::DataType>& value_type, bool ordered) {
  return ValueOrStop(arrow::DictionaryType::Make(index_type, value_type, ordered));
}

// [[arrow::export]]
std::shared_ptr<arrow::DataType> DictionaryType__index_type(
    const std::shared_ptr<arrow::DictionaryType>& type) {
  return type->index_type();
}

// [[arrow::export]]
std::shared_ptr<arrow::DataType> DictionaryType__value_type(
    const std::shared_ptr<arrow::DictionaryType>& type) {
  return type->value_type();
}

// [[arrow::export]]
std::string DictionaryType__name(const std::shared_ptr<arrow::DictionaryType>& type) {
  return type->name();
}

// [[arrow::export]]
bool DictionaryType__ordered(const std::shared_ptr<arrow::DictionaryType>& type) {
  return type->ordered();
}

// [[arrow::export]]
std::shared_ptr<arrow::Field> StructType__GetFieldByName(
    const std::shared_ptr<arrow::StructType>& type, const std::string& name) {
  return type->GetFieldByName(name);
}

// [[arrow::export]]
int StructType__GetFieldIndex(const std::shared_ptr<arrow::StructType>& type,
                              const std::string& name) {
  return type->GetFieldIndex(name);
}

// [[arrow::export]]
std::shared_ptr<arrow::Field> ListType__value_field(
    const std::shared_ptr<arrow::ListType>& type) {
  return type->value_field();
}

// [[arrow::export]]
std::shared_ptr<arrow::DataType> ListType__value_type(
    const std::shared_ptr<arrow::ListType>& type) {
  return type->value_type();
}

// [[arrow::export]]
std::shared_ptr<arrow::Field> LargeListType__value_field(
    const std::shared_ptr<arrow::LargeListType>& type) {
  return type->value_field();
}

// [[arrow::export]]
std::shared_ptr<arrow::DataType> LargeListType__value_type(
    const std::shared_ptr<arrow::LargeListType>& type) {
  return type->value_type();
}

// [[arrow::export]]
std::shared_ptr<arrow::Field> FixedSizeListType__value_field(
    const std::shared_ptr<arrow::FixedSizeListType>& type) {
  return type->value_field();
}

// [[arrow::export]]
std::shared_ptr<arrow::DataType> FixedSizeListType__value_type(
    const std::shared_ptr<arrow::FixedSizeListType>& type) {
  return type->value_type();
}

// [[arrow::export]]
int FixedSizeListType__list_size(const std::shared_ptr<arrow::FixedSizeListType>& type) {
  return type->list_size();
}

#endif
