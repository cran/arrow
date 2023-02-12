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

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "arrow/compare.h"
#include "arrow/compute/api_scalar.h"
#include "arrow/compute/cast.h"
#include "arrow/compute/kernels/base_arithmetic_internal.h"
#include "arrow/compute/kernels/common.h"
#include "arrow/compute/kernels/util_internal.h"
#include "arrow/type.h"
#include "arrow/type_traits.h"
#include "arrow/util/decimal.h"
#include "arrow/util/int_util_overflow.h"
#include "arrow/util/macros.h"
#include "arrow/visit_scalar_inline.h"

namespace arrow {

using internal::AddWithOverflow;
using internal::DivideWithOverflow;
using internal::MultiplyWithOverflow;
using internal::NegateWithOverflow;
using internal::SubtractWithOverflow;

namespace compute {
namespace internal {

using applicator::ScalarBinary;
using applicator::ScalarBinaryEqualTypes;
using applicator::ScalarBinaryNotNull;
using applicator::ScalarBinaryNotNullEqualTypes;
using applicator::ScalarUnary;
using applicator::ScalarUnaryNotNull;
using applicator::ScalarUnaryNotNullStateful;

namespace {

// Convenience visitor to detect if a numeric Scalar is positive.
struct IsPositiveVisitor {
  bool result = false;

  template <typename... Ts>
  Status Visit(const NumericScalar<Ts...>& scalar) {
    result = scalar.value > 0;
    return Status::OK();
  }
  template <typename... Ts>
  Status Visit(const DecimalScalar<Ts...>& scalar) {
    result = scalar.value > 0;
    return Status::OK();
  }
  Status Visit(const Scalar& scalar) { return Status::OK(); }
};

bool IsPositive(const Scalar& scalar) {
  IsPositiveVisitor visitor{};
  std::ignore = VisitScalarInline(scalar, &visitor);
  return visitor.result;
}

// N.B. take care not to conflict with type_traits.h as that can cause surprises in a
// unity build

struct RoundUtil {
  // Calculate powers of ten with arbitrary integer exponent
  template <typename T = double>
  static enable_if_floating_value<T> Pow10(int64_t power) {
    static constexpr T lut[] = {1e0F, 1e1F, 1e2F,  1e3F,  1e4F,  1e5F,  1e6F,  1e7F,
                                1e8F, 1e9F, 1e10F, 1e11F, 1e12F, 1e13F, 1e14F, 1e15F};
    int64_t lut_size = (sizeof(lut) / sizeof(*lut));
    int64_t abs_power = std::abs(power);
    auto pow10 = lut[std::min(abs_power, lut_size - 1)];
    while (abs_power-- >= lut_size) {
      pow10 *= 1e1F;
    }
    return (power >= 0) ? pow10 : (1 / pow10);
  }
};

// Specializations of rounding implementations for round kernels
template <typename Type, RoundMode>
struct RoundImpl;

template <typename Type>
struct RoundImpl<Type, RoundMode::DOWN> {
  template <typename T = Type>
  static constexpr enable_if_floating_value<T> Round(const T val) {
    return std::floor(val);
  }

  template <typename T = Type>
  static enable_if_decimal_value<T, void> Round(T* val, const T& remainder,
                                                const T& pow10, const int32_t scale) {
    (*val) -= remainder;
    if (remainder.Sign() < 0) {
      (*val) -= pow10;
    }
  }
};

template <typename Type>
struct RoundImpl<Type, RoundMode::UP> {
  template <typename T = Type>
  static constexpr enable_if_floating_value<T> Round(const T val) {
    return std::ceil(val);
  }

  template <typename T = Type>
  static enable_if_decimal_value<T, void> Round(T* val, const T& remainder,
                                                const T& pow10, const int32_t scale) {
    (*val) -= remainder;
    if (remainder.Sign() > 0 && remainder != 0) {
      (*val) += pow10;
    }
  }
};

template <typename Type>
struct RoundImpl<Type, RoundMode::TOWARDS_ZERO> {
  template <typename T = Type>
  static constexpr enable_if_floating_value<T> Round(const T val) {
    return std::trunc(val);
  }

  template <typename T = Type>
  static enable_if_decimal_value<T, void> Round(T* val, const T& remainder,
                                                const T& pow10, const int32_t scale) {
    (*val) -= remainder;
  }
};

template <typename Type>
struct RoundImpl<Type, RoundMode::TOWARDS_INFINITY> {
  template <typename T = Type>
  static constexpr enable_if_floating_value<T> Round(const T val) {
    return std::signbit(val) ? std::floor(val) : std::ceil(val);
  }

  template <typename T = Type>
  static enable_if_decimal_value<T, void> Round(T* val, const T& remainder,
                                                const T& pow10, const int32_t scale) {
    (*val) -= remainder;
    if (remainder.Sign() < 0) {
      (*val) -= pow10;
    } else if (remainder.Sign() > 0 && remainder != 0) {
      (*val) += pow10;
    }
  }
};

// NOTE: RoundImpl variants for the HALF_* rounding modes are only
// invoked when the fractional part is equal to 0.5 (std::round is invoked
// otherwise).

template <typename Type>
struct RoundImpl<Type, RoundMode::HALF_DOWN> {
  template <typename T = Type>
  static constexpr enable_if_floating_value<T> Round(const T val) {
    return RoundImpl<T, RoundMode::DOWN>::Round(val);
  }

  template <typename T = Type>
  static enable_if_decimal_value<T, void> Round(T* val, const T& remainder,
                                                const T& pow10, const int32_t scale) {
    RoundImpl<T, RoundMode::DOWN>::Round(val, remainder, pow10, scale);
  }
};

template <typename Type>
struct RoundImpl<Type, RoundMode::HALF_UP> {
  template <typename T = Type>
  static constexpr enable_if_floating_value<T> Round(const T val) {
    return RoundImpl<T, RoundMode::UP>::Round(val);
  }

  template <typename T = Type>
  static enable_if_decimal_value<T, void> Round(T* val, const T& remainder,
                                                const T& pow10, const int32_t scale) {
    RoundImpl<T, RoundMode::UP>::Round(val, remainder, pow10, scale);
  }
};

template <typename Type>
struct RoundImpl<Type, RoundMode::HALF_TOWARDS_ZERO> {
  template <typename T = Type>
  static constexpr enable_if_floating_value<T> Round(const T val) {
    return RoundImpl<T, RoundMode::TOWARDS_ZERO>::Round(val);
  }

  template <typename T = Type>
  static enable_if_decimal_value<T, void> Round(T* val, const T& remainder,
                                                const T& pow10, const int32_t scale) {
    RoundImpl<T, RoundMode::TOWARDS_ZERO>::Round(val, remainder, pow10, scale);
  }
};

template <typename Type>
struct RoundImpl<Type, RoundMode::HALF_TOWARDS_INFINITY> {
  template <typename T = Type>
  static constexpr enable_if_floating_value<T> Round(const T val) {
    return RoundImpl<T, RoundMode::TOWARDS_INFINITY>::Round(val);
  }

  template <typename T = Type>
  static enable_if_decimal_value<T, void> Round(T* val, const T& remainder,
                                                const T& pow10, const int32_t scale) {
    RoundImpl<T, RoundMode::TOWARDS_INFINITY>::Round(val, remainder, pow10, scale);
  }
};

template <typename Type>
struct RoundImpl<Type, RoundMode::HALF_TO_EVEN> {
  template <typename T = Type>
  static constexpr enable_if_floating_value<T> Round(const T val) {
    return std::round(val * T(0.5)) * 2;
  }

  template <typename T = Type>
  static enable_if_decimal_value<T, void> Round(T* val, const T& remainder,
                                                const T& pow10, const int32_t scale) {
    auto scaled = val->ReduceScaleBy(scale, /*round=*/false);
    if (scaled.low_bits() % 2 != 0) {
      scaled += remainder.Sign() >= 0 ? 1 : -1;
    }
    *val = scaled.IncreaseScaleBy(scale);
  }
};

template <typename Type>
struct RoundImpl<Type, RoundMode::HALF_TO_ODD> {
  template <typename T = Type>
  static constexpr enable_if_floating_value<T> Round(const T val) {
    return std::floor(val * T(0.5)) + std::ceil(val * T(0.5));
  }

  template <typename T = Type>
  static enable_if_decimal_value<T, void> Round(T* val, const T& remainder,
                                                const T& pow10, const int32_t scale) {
    auto scaled = val->ReduceScaleBy(scale, /*round=*/false);
    if (scaled.low_bits() % 2 == 0) {
      scaled += remainder.Sign() ? 1 : -1;
    }
    *val = scaled.IncreaseScaleBy(scale);
  }
};

// Specializations of kernel state for round kernels
template <typename OptionsType>
struct RoundOptionsWrapper;

template <>
struct RoundOptionsWrapper<RoundOptions> : public OptionsWrapper<RoundOptions> {
  using OptionsType = RoundOptions;
  double pow10;

  explicit RoundOptionsWrapper(OptionsType options) : OptionsWrapper(std::move(options)) {
    // Only positive exponents for powers of 10 are used because combining
    // multiply and division operations produced more stable rounding than
    // using multiply-only.  Refer to NumPy's round implementation:
    // https://github.com/numpy/numpy/blob/7b2f20b406d27364c812f7a81a9c901afbd3600c/numpy/core/src/multiarray/calculation.c#L589
    pow10 = RoundUtil::Pow10(std::abs(options.ndigits));
  }

  static Result<std::unique_ptr<KernelState>> Init(KernelContext* ctx,
                                                   const KernelInitArgs& args) {
    if (auto options = static_cast<const OptionsType*>(args.options)) {
      return std::make_unique<RoundOptionsWrapper>(*options);
    }
    return Status::Invalid(
        "Attempted to initialize KernelState from null FunctionOptions");
  }
};

template <>
struct RoundOptionsWrapper<RoundToMultipleOptions>
    : public OptionsWrapper<RoundToMultipleOptions> {
  using OptionsType = RoundToMultipleOptions;
  using OptionsWrapper::OptionsWrapper;

  static Result<std::unique_ptr<KernelState>> Init(KernelContext* ctx,
                                                   const KernelInitArgs& args) {
    auto options = static_cast<const OptionsType*>(args.options);
    if (!options) {
      return Status::Invalid(
          "Attempted to initialize KernelState from null FunctionOptions");
    }

    const auto& multiple = options->multiple;
    if (!multiple || !multiple->is_valid) {
      return Status::Invalid("Rounding multiple must be non-null and valid");
    }

    if (!IsPositive(*multiple)) {
      return Status::Invalid("Rounding multiple must be positive");
    }

    // Ensure the rounding multiple option matches the kernel's output type.
    // The output type is not available here so we use the following rule:
    // If `multiple` is neither a floating-point nor a decimal type, then
    // cast to float64, else cast to the kernel's input type.
    std::shared_ptr<DataType> to_type =
        (!is_floating(multiple->type->id()) && !is_decimal(multiple->type->id()))
            ? float64()
            : args.inputs[0].GetSharedPtr();
    if (!multiple->type->Equals(to_type)) {
      ARROW_ASSIGN_OR_RAISE(
          auto casted_multiple,
          Cast(Datum(multiple), to_type, CastOptions::Safe(), ctx->exec_context()));

      // Create a new option object if the rounding multiple was casted.
      auto new_options = OptionsType(casted_multiple.scalar(), options->round_mode);
      return std::make_unique<RoundOptionsWrapper>(new_options);
    }

    return std::make_unique<RoundOptionsWrapper>(*options);
  }
};

template <typename ArrowType, RoundMode RndMode, typename Enable = void>
struct Round {
  using CType = typename TypeTraits<ArrowType>::CType;
  using State = RoundOptionsWrapper<RoundOptions>;

  CType pow10;
  int64_t ndigits;

  explicit Round(const State& state, const DataType& out_ty)
      : pow10(static_cast<CType>(state.pow10)), ndigits(state.options.ndigits) {}

  template <typename T = ArrowType, typename CType = typename TypeTraits<T>::CType>
  enable_if_floating_value<CType> Call(KernelContext* ctx, CType arg, Status* st) const {
    // Do not process Inf or NaN because they will trigger the overflow error at end of
    // function.
    if (!std::isfinite(arg)) {
      return arg;
    }
    auto round_val = ndigits >= 0 ? (arg * pow10) : (arg / pow10);
    auto frac = round_val - std::floor(round_val);
    if (frac != T(0)) {
      // Use std::round() if in tie-breaking mode and scaled value is not 0.5.
      if ((RndMode >= RoundMode::HALF_DOWN) && (frac != T(0.5))) {
        round_val = std::round(round_val);
      } else {
        round_val = RoundImpl<CType, RndMode>::Round(round_val);
      }
      // Equality check is ommitted so that the common case of 10^0 (integer rounding)
      // uses multiply-only
      round_val = ndigits > 0 ? (round_val / pow10) : (round_val * pow10);
      if (!std::isfinite(round_val)) {
        *st = Status::Invalid("overflow occurred during rounding");
        return arg;
      }
    } else {
      // If scaled value is an integer, then no rounding is needed.
      round_val = arg;
    }
    return round_val;
  }
};

template <typename ArrowType, RoundMode kRoundMode>
struct Round<ArrowType, kRoundMode, enable_if_decimal<ArrowType>> {
  using CType = typename TypeTraits<ArrowType>::CType;
  using State = RoundOptionsWrapper<RoundOptions>;

  const ArrowType& ty;
  int64_t ndigits;
  int32_t pow;
  // pow10 is "1" for the given decimal scale. Similarly half_pow10 is "0.5".
  CType pow10, half_pow10, neg_half_pow10;

  explicit Round(const State& state, const DataType& out_ty)
      : Round(state.options.ndigits, out_ty) {}

  explicit Round(int64_t ndigits, const DataType& out_ty)
      : ty(checked_cast<const ArrowType&>(out_ty)),
        ndigits(ndigits),
        pow(static_cast<int32_t>(ty.scale() - ndigits)) {
    if (pow >= ty.precision() || pow < 0) {
      pow10 = half_pow10 = neg_half_pow10 = 0;
    } else {
      pow10 = CType::GetScaleMultiplier(pow);
      half_pow10 = CType::GetHalfScaleMultiplier(pow);
      neg_half_pow10 = -half_pow10;
    }
  }

  template <typename T = ArrowType, typename CType = typename TypeTraits<T>::CType>
  enable_if_decimal_value<CType> Call(KernelContext* ctx, CType arg, Status* st) const {
    if (pow >= ty.precision()) {
      *st = Status::Invalid("Rounding to ", ndigits,
                            " digits will not fit in precision of ", ty);
      return 0;
    } else if (pow < 0) {
      // no-op, copy output to input
      return arg;
    }

    std::pair<CType, CType> pair;
    *st = arg.Divide(pow10).Value(&pair);
    if (!st->ok()) return arg;
    // The remainder is effectively the scaled fractional part after division.
    const auto& remainder = pair.second;
    if (remainder == 0) return arg;
    if (kRoundMode >= RoundMode::HALF_DOWN) {
      if (remainder == half_pow10 || remainder == neg_half_pow10) {
        // On the halfway point, use tiebreaker
        RoundImpl<CType, kRoundMode>::Round(&arg, remainder, pow10, pow);
      } else if (remainder.Sign() >= 0) {
        // Positive, round up/down
        arg -= remainder;
        if (remainder > half_pow10) {
          arg += pow10;
        }
      } else {
        // Negative, round up/down
        arg -= remainder;
        if (remainder < neg_half_pow10) {
          arg -= pow10;
        }
      }
    } else {
      RoundImpl<CType, kRoundMode>::Round(&arg, remainder, pow10, pow);
    }
    if (!arg.FitsInPrecision(ty.precision())) {
      *st = Status::Invalid("Rounded value ", arg.ToString(ty.scale()),
                            " does not fit in precision of ", ty);
      return 0;
    }
    return arg;
  }
};

template <typename DecimalType, RoundMode kMode, int32_t kDigits>
Status FixedRoundDecimalExec(KernelContext* ctx, const ExecSpan& batch, ExecResult* out) {
  using Op = Round<DecimalType, kMode>;
  return ScalarUnaryNotNullStateful<DecimalType, DecimalType, Op>(
             Op(kDigits, *out->type()))
      .Exec(ctx, batch, out);
}

template <typename ArrowType, RoundMode kRoundMode, typename Enable = void>
struct RoundToMultiple {
  using CType = typename TypeTraits<ArrowType>::CType;
  using State = RoundOptionsWrapper<RoundToMultipleOptions>;

  CType multiple;

  explicit RoundToMultiple(const State& state, const DataType& out_ty)
      : multiple(UnboxScalar<ArrowType>::Unbox(*state.options.multiple)) {
    const auto& options = state.options;
    DCHECK(options.multiple);
    DCHECK(options.multiple->is_valid);
    DCHECK(is_floating(options.multiple->type->id()));
  }

  template <typename T = ArrowType, typename CType = typename TypeTraits<T>::CType>
  enable_if_floating_value<CType> Call(KernelContext* ctx, CType arg, Status* st) const {
    // Do not process Inf or NaN because they will trigger the overflow error at end of
    // function.
    if (!std::isfinite(arg)) {
      return arg;
    }
    auto round_val = arg / multiple;
    auto frac = round_val - std::floor(round_val);
    if (frac != T(0)) {
      // Use std::round() if in tie-breaking mode and scaled value is not 0.5.
      if ((kRoundMode >= RoundMode::HALF_DOWN) && (frac != T(0.5))) {
        round_val = std::round(round_val);
      } else {
        round_val = RoundImpl<CType, kRoundMode>::Round(round_val);
      }
      round_val *= multiple;
      if (!std::isfinite(round_val)) {
        *st = Status::Invalid("overflow occurred during rounding");
        return arg;
      }
    } else {
      // If scaled value is an integer, then no rounding is needed.
      round_val = arg;
    }
    return round_val;
  }
};

template <typename ArrowType, RoundMode kRoundMode>
struct RoundToMultiple<ArrowType, kRoundMode, enable_if_decimal<ArrowType>> {
  using CType = typename TypeTraits<ArrowType>::CType;
  using State = RoundOptionsWrapper<RoundToMultipleOptions>;

  const ArrowType& ty;
  CType multiple, half_multiple, neg_half_multiple;
  bool has_halfway_point;

  explicit RoundToMultiple(const State& state, const DataType& out_ty)
      : ty(checked_cast<const ArrowType&>(out_ty)),
        multiple(UnboxScalar<ArrowType>::Unbox(*state.options.multiple)),
        half_multiple(multiple / 2),
        neg_half_multiple(-half_multiple),
        has_halfway_point(multiple.low_bits() % 2 == 0) {
    const auto& options = state.options;
    DCHECK(options.multiple);
    DCHECK(options.multiple->is_valid);
    DCHECK(options.multiple->type->Equals(out_ty));
  }

  template <typename T = ArrowType, typename CType = typename TypeTraits<T>::CType>
  enable_if_decimal_value<CType> Call(KernelContext* ctx, CType arg, Status* st) const {
    std::pair<CType, CType> pair;
    *st = arg.Divide(multiple).Value(&pair);
    if (!st->ok()) return arg;
    const auto& remainder = pair.second;
    if (remainder == 0) return arg;
    if (kRoundMode >= RoundMode::HALF_DOWN) {
      if (has_halfway_point &&
          (remainder == half_multiple || remainder == neg_half_multiple)) {
        // On the halfway point, use tiebreaker
        // Manually implement rounding since we're not actually rounding a
        // decimal value, but rather manipulating the multiple
        switch (kRoundMode) {
          case RoundMode::HALF_DOWN:
            if (remainder.Sign() < 0) pair.first -= 1;
            break;
          case RoundMode::HALF_UP:
            if (remainder.Sign() >= 0) pair.first += 1;
            break;
          case RoundMode::HALF_TOWARDS_ZERO:
            // Do nothing
            break;
          case RoundMode::HALF_TOWARDS_INFINITY:
            pair.first += remainder.Sign() >= 0 ? 1 : -1;
            break;
          case RoundMode::HALF_TO_EVEN:
            if (pair.first.low_bits() % 2 != 0) {
              pair.first += remainder.Sign() >= 0 ? 1 : -1;
            }
            break;
          case RoundMode::HALF_TO_ODD:
            if (pair.first.low_bits() % 2 == 0) {
              pair.first += remainder.Sign() >= 0 ? 1 : -1;
            }
            break;
          default:
            DCHECK(false);
        }
      } else if (remainder.Sign() >= 0) {
        // Positive, round up/down
        if (remainder > half_multiple) {
          pair.first += 1;
        }
      } else {
        // Negative, round up/down
        if (remainder < neg_half_multiple) {
          pair.first -= 1;
        }
      }
    } else {
      // Manually implement rounding since we're not actually rounding a
      // decimal value, but rather manipulating the multiple
      switch (kRoundMode) {
        case RoundMode::DOWN:
          if (remainder.Sign() < 0) pair.first -= 1;
          break;
        case RoundMode::UP:
          if (remainder.Sign() >= 0) pair.first += 1;
          break;
        case RoundMode::TOWARDS_ZERO:
          // Do nothing
          break;
        case RoundMode::TOWARDS_INFINITY:
          pair.first += remainder.Sign() >= 0 ? 1 : -1;
          break;
        default:
          DCHECK(false);
      }
    }
    CType round_val = pair.first * multiple;
    if (!round_val.FitsInPrecision(ty.precision())) {
      *st = Status::Invalid("Rounded value ", round_val.ToString(ty.scale()),
                            " does not fit in precision of ", ty);
      return 0;
    }
    return round_val;
  }
};

struct Floor {
  template <typename T, typename Arg>
  static constexpr enable_if_floating_value<Arg, T> Call(KernelContext*, Arg arg,
                                                         Status*) {
    static_assert(std::is_same<T, Arg>::value, "");
    return RoundImpl<T, RoundMode::DOWN>::Round(arg);
  }
};

struct Ceil {
  template <typename T, typename Arg>
  static constexpr enable_if_floating_value<Arg, T> Call(KernelContext*, Arg arg,
                                                         Status*) {
    static_assert(std::is_same<T, Arg>::value, "");
    return RoundImpl<T, RoundMode::UP>::Round(arg);
  }
};

struct Trunc {
  template <typename T, typename Arg>
  static constexpr enable_if_floating_value<Arg, T> Call(KernelContext*, Arg arg,
                                                         Status*) {
    static_assert(std::is_same<T, Arg>::value, "");
    return RoundImpl<T, RoundMode::TOWARDS_ZERO>::Round(arg);
  }
};

// Generate a kernel given a bitwise arithmetic functor. Assumes the
// functor treats all integer types of equal width identically
template <template <typename... Args> class KernelGenerator, typename Op>
ArrayKernelExec TypeAgnosticBitWiseExecFromOp(detail::GetTypeId get_id) {
  switch (get_id.id) {
    case Type::INT8:
    case Type::UINT8:
      return KernelGenerator<UInt8Type, UInt8Type, Op>::Exec;
    case Type::INT16:
    case Type::UINT16:
      return KernelGenerator<UInt16Type, UInt16Type, Op>::Exec;
    case Type::INT32:
    case Type::UINT32:
      return KernelGenerator<UInt32Type, UInt32Type, Op>::Exec;
    case Type::INT64:
    case Type::UINT64:
      return KernelGenerator<UInt64Type, UInt64Type, Op>::Exec;
    default:
      DCHECK(false);
      return nullptr;
  }
}

template <template <typename... Args> class KernelGenerator, typename Op>
ArrayKernelExec ShiftExecFromOp(detail::GetTypeId get_id) {
  switch (get_id.id) {
    case Type::INT8:
      return KernelGenerator<Int8Type, Int8Type, Op>::Exec;
    case Type::UINT8:
      return KernelGenerator<UInt8Type, UInt8Type, Op>::Exec;
    case Type::INT16:
      return KernelGenerator<Int16Type, Int16Type, Op>::Exec;
    case Type::UINT16:
      return KernelGenerator<UInt16Type, UInt16Type, Op>::Exec;
    case Type::INT32:
      return KernelGenerator<Int32Type, Int32Type, Op>::Exec;
    case Type::UINT32:
      return KernelGenerator<UInt32Type, UInt32Type, Op>::Exec;
    case Type::INT64:
      return KernelGenerator<Int64Type, Int64Type, Op>::Exec;
    case Type::UINT64:
      return KernelGenerator<UInt64Type, UInt64Type, Op>::Exec;
    default:
      DCHECK(false);
      return nullptr;
  }
}

template <template <typename... Args> class KernelGenerator, typename Op>
ArrayKernelExec GenerateArithmeticFloatingPoint(detail::GetTypeId get_id) {
  switch (get_id.id) {
    case Type::FLOAT:
      return KernelGenerator<FloatType, FloatType, Op>::Exec;
    case Type::DOUBLE:
      return KernelGenerator<DoubleType, DoubleType, Op>::Exec;
    default:
      DCHECK(false);
      return nullptr;
  }
}

// resolve decimal binary operation output type per *casted* args
template <typename OutputGetter>
Result<TypeHolder> ResolveDecimalBinaryOperationOutput(
    const std::vector<TypeHolder>& types, OutputGetter&& getter) {
  // casted types should be same size decimals
  const auto& left_type = checked_cast<const DecimalType&>(*types[0]);
  const auto& right_type = checked_cast<const DecimalType&>(*types[1]);
  DCHECK_EQ(left_type.id(), right_type.id());

  int32_t precision, scale;
  std::tie(precision, scale) = getter(left_type.precision(), left_type.scale(),
                                      right_type.precision(), right_type.scale());
  ARROW_ASSIGN_OR_RAISE(auto type, DecimalType::Make(left_type.id(), precision, scale));
  return std::move(type);
}

template <typename Op>
void AddDecimalUnaryKernels(ScalarFunction* func) {
  OutputType out_type(FirstType);
  auto in_type128 = InputType(Type::DECIMAL128);
  auto in_type256 = InputType(Type::DECIMAL256);
  auto exec128 = ScalarUnaryNotNull<Decimal128Type, Decimal128Type, Op>::Exec;
  auto exec256 = ScalarUnaryNotNull<Decimal256Type, Decimal256Type, Op>::Exec;
  DCHECK_OK(func->AddKernel({in_type128}, out_type, exec128));
  DCHECK_OK(func->AddKernel({in_type256}, out_type, exec256));
}

// Generate a kernel given an arithmetic functor
template <template <typename...> class KernelGenerator, typename OutType, typename Op>
ArrayKernelExec GenerateArithmeticWithFixedIntOutType(detail::GetTypeId get_id) {
  switch (get_id.id) {
    case Type::INT8:
      return KernelGenerator<OutType, Int8Type, Op>::Exec;
    case Type::UINT8:
      return KernelGenerator<OutType, UInt8Type, Op>::Exec;
    case Type::INT16:
      return KernelGenerator<OutType, Int16Type, Op>::Exec;
    case Type::UINT16:
      return KernelGenerator<OutType, UInt16Type, Op>::Exec;
    case Type::INT32:
      return KernelGenerator<OutType, Int32Type, Op>::Exec;
    case Type::UINT32:
      return KernelGenerator<OutType, UInt32Type, Op>::Exec;
    case Type::INT64:
    case Type::TIMESTAMP:
      return KernelGenerator<OutType, Int64Type, Op>::Exec;
    case Type::UINT64:
      return KernelGenerator<OutType, UInt64Type, Op>::Exec;
    case Type::FLOAT:
      return KernelGenerator<FloatType, FloatType, Op>::Exec;
    case Type::DOUBLE:
      return KernelGenerator<DoubleType, DoubleType, Op>::Exec;
    default:
      DCHECK(false);
      return nullptr;
  }
}

struct ArithmeticFunction : ScalarFunction {
  using ScalarFunction::ScalarFunction;

  Result<const Kernel*> DispatchBest(std::vector<TypeHolder>* types) const override {
    RETURN_NOT_OK(CheckArity(types->size()));

    RETURN_NOT_OK(CheckDecimals(types));

    using arrow::compute::detail::DispatchExactImpl;
    if (auto kernel = DispatchExactImpl(this, *types)) return kernel;

    EnsureDictionaryDecoded(types);

    // Only promote types for binary functions
    if (types->size() == 2) {
      ReplaceNullWithOtherType(types);
      TimeUnit::type finest_unit;
      if (CommonTemporalResolution(types->data(), types->size(), &finest_unit)) {
        ReplaceTemporalTypes(finest_unit, types);
      } else {
        if (TypeHolder type = CommonNumeric(*types)) {
          ReplaceTypes(type, types);
        }
      }
    }

    if (auto kernel = DispatchExactImpl(this, *types)) return kernel;
    return arrow::compute::detail::NoMatchingKernel(this, *types);
  }

  Status CheckDecimals(std::vector<TypeHolder>* types) const {
    if (!HasDecimal(*types)) return Status::OK();

    if (types->size() == 2) {
      // "add_checked" -> "add"
      const auto func_name = name();
      const std::string op = func_name.substr(0, func_name.find("_"));
      if (op == "add" || op == "subtract") {
        return CastBinaryDecimalArgs(DecimalPromotion::kAdd, types);
      } else if (op == "multiply") {
        return CastBinaryDecimalArgs(DecimalPromotion::kMultiply, types);
      } else if (op == "divide") {
        return CastBinaryDecimalArgs(DecimalPromotion::kDivide, types);
      } else {
        return Status::Invalid("Invalid decimal function: ", func_name);
      }
    }
    return Status::OK();
  }
};

/// An ArithmeticFunction that promotes only decimal arguments to double.
struct ArithmeticDecimalToFloatingPointFunction : public ArithmeticFunction {
  using ArithmeticFunction::ArithmeticFunction;

  Result<const Kernel*> DispatchBest(std::vector<TypeHolder>* types) const override {
    RETURN_NOT_OK(CheckArity(types->size()));

    using arrow::compute::detail::DispatchExactImpl;
    if (auto kernel = DispatchExactImpl(this, *types)) return kernel;

    EnsureDictionaryDecoded(types);

    if (types->size() == 2) {
      ReplaceNullWithOtherType(types);
    }

    for (size_t i = 0; i < types->size(); ++i) {
      if (is_decimal((*types)[i].type->id())) {
        (*types)[i] = float64();
      }
    }

    if (TypeHolder type = CommonNumeric(*types)) {
      ReplaceTypes(type, types);
    }

    if (auto kernel = DispatchExactImpl(this, *types)) return kernel;
    return arrow::compute::detail::NoMatchingKernel(this, *types);
  }
};

/// An ArithmeticFunction that promotes only integer arguments to double.
struct ArithmeticIntegerToFloatingPointFunction : public ArithmeticFunction {
  using ArithmeticFunction::ArithmeticFunction;

  Result<const Kernel*> DispatchBest(std::vector<TypeHolder>* types) const override {
    RETURN_NOT_OK(CheckArity(types->size()));
    RETURN_NOT_OK(CheckDecimals(types));

    using arrow::compute::detail::DispatchExactImpl;
    if (auto kernel = DispatchExactImpl(this, *types)) return kernel;

    EnsureDictionaryDecoded(types);

    if (types->size() == 2) {
      ReplaceNullWithOtherType(types);
    }

    for (size_t i = 0; i < types->size(); ++i) {
      if (is_integer((*types)[i].type->id())) {
        (*types)[i] = float64();
      }
    }

    if (auto type = CommonNumeric(*types)) {
      ReplaceTypes(type, types);
    }

    if (auto kernel = DispatchExactImpl(this, *types)) return kernel;
    return arrow::compute::detail::NoMatchingKernel(this, *types);
  }
};

/// An ArithmeticFunction that promotes integer and decimal arguments to double.
struct ArithmeticFloatingPointFunction : public ArithmeticFunction {
  using ArithmeticFunction::ArithmeticFunction;

  Result<const Kernel*> DispatchBest(std::vector<TypeHolder>* types) const override {
    RETURN_NOT_OK(CheckArity(types->size()));

    using arrow::compute::detail::DispatchExactImpl;
    if (auto kernel = DispatchExactImpl(this, *types)) return kernel;

    EnsureDictionaryDecoded(types);

    if (types->size() == 2) {
      ReplaceNullWithOtherType(types);
    }

    for (size_t i = 0; i < types->size(); ++i) {
      if (is_integer((*types)[i].type->id()) || is_decimal((*types)[i].type->id())) {
        (*types)[i] = float64();
      }
    }

    if (auto type = CommonNumeric(*types)) {
      ReplaceTypes(type, types);
    }

    if (auto kernel = DispatchExactImpl(this, *types)) return kernel;
    return arrow::compute::detail::NoMatchingKernel(this, *types);
  }
};

// A scalar kernel that ignores (assumed all-null) inputs and returns null.
Status NullToNullExec(KernelContext* ctx, const ExecSpan& batch, ExecResult* out) {
  return Status::OK();
}

void AddNullExec(ScalarFunction* func) {
  std::vector<InputType> input_types(func->arity().num_args, InputType(Type::NA));
  DCHECK_OK(func->AddKernel(std::move(input_types), OutputType(null()), NullToNullExec));
}

template <typename Op, typename FunctionImpl = ArithmeticFunction>
std::shared_ptr<ScalarFunction> MakeArithmeticFunction(std::string name,
                                                       FunctionDoc doc) {
  auto func = std::make_shared<FunctionImpl>(name, Arity::Binary(), std::move(doc));
  for (const auto& ty : NumericTypes()) {
    auto exec = ArithmeticExecFromOp<ScalarBinaryEqualTypes, Op>(ty);
    DCHECK_OK(func->AddKernel({ty, ty}, ty, exec));
  }
  AddNullExec(func.get());
  return func;
}

// Like MakeArithmeticFunction, but for arithmetic ops that need to run
// only on non-null output.
template <typename Op, typename FunctionImpl = ArithmeticFunction>
std::shared_ptr<ScalarFunction> MakeArithmeticFunctionNotNull(std::string name,
                                                              FunctionDoc doc) {
  auto func = std::make_shared<FunctionImpl>(name, Arity::Binary(), std::move(doc));
  for (const auto& ty : NumericTypes()) {
    auto exec = ArithmeticExecFromOp<ScalarBinaryNotNullEqualTypes, Op>(ty);
    DCHECK_OK(func->AddKernel({ty, ty}, ty, exec));
  }
  AddNullExec(func.get());
  return func;
}

template <typename Op>
std::shared_ptr<ScalarFunction> MakeUnaryArithmeticFunction(std::string name,
                                                            FunctionDoc doc) {
  auto func = std::make_shared<ArithmeticFunction>(name, Arity::Unary(), std::move(doc));
  for (const auto& ty : NumericTypes()) {
    auto exec = ArithmeticExecFromOp<ScalarUnary, Op>(ty);
    DCHECK_OK(func->AddKernel({ty}, ty, exec));
  }
  AddNullExec(func.get());
  return func;
}

// Like MakeUnaryArithmeticFunction, but for unary arithmetic ops with a fixed
// output type for integral inputs.
template <typename Op, typename IntOutType>
std::shared_ptr<ScalarFunction> MakeUnaryArithmeticFunctionWithFixedIntOutType(
    std::string name, FunctionDoc doc) {
  auto int_out_ty = TypeTraits<IntOutType>::type_singleton();
  auto func = std::make_shared<ArithmeticFunction>(name, Arity::Unary(), std::move(doc));
  for (const auto& ty : NumericTypes()) {
    auto out_ty = arrow::is_floating(ty->id()) ? ty : int_out_ty;
    auto exec = GenerateArithmeticWithFixedIntOutType<ScalarUnary, IntOutType, Op>(ty);
    DCHECK_OK(func->AddKernel({ty}, out_ty, exec));
  }
  {
    auto exec = ScalarUnary<Int64Type, Decimal128Type, Op>::Exec;
    DCHECK_OK(func->AddKernel({InputType(Type::DECIMAL128)}, int64(), exec));
    exec = ScalarUnary<Int64Type, Decimal256Type, Op>::Exec;
    DCHECK_OK(func->AddKernel({InputType(Type::DECIMAL256)}, int64(), exec));
  }
  AddNullExec(func.get());
  return func;
}

// Like MakeUnaryArithmeticFunction, but for arithmetic ops that need to run
// only on non-null output.
template <typename Op>
std::shared_ptr<ScalarFunction> MakeUnaryArithmeticFunctionNotNull(std::string name,
                                                                   FunctionDoc doc) {
  auto func = std::make_shared<ArithmeticFunction>(name, Arity::Unary(), std::move(doc));
  for (const auto& ty : NumericTypes()) {
    auto exec = ArithmeticExecFromOp<ScalarUnaryNotNull, Op>(ty);
    DCHECK_OK(func->AddKernel({ty}, ty, exec));
  }
  AddNullExec(func.get());
  return func;
}

#define ROUND_CASE(MODE)                                                       \
  case RoundMode::MODE: {                                                      \
    using Op = OpImpl<Type, RoundMode::MODE>;                                  \
    return ScalarUnaryNotNullStateful<Type, Type, Op>(Op(state, *out->type())) \
        .Exec(ctx, batch, out);                                                \
  }

// Exec the round kernel for the given types
template <typename Type, typename OptionsType,
          template <typename, RoundMode, typename...> class OpImpl>
struct RoundKernel {
  static Status Exec(KernelContext* ctx, const ExecSpan& batch, ExecResult* out) {
    using State = RoundOptionsWrapper<OptionsType>;
    const auto& state = static_cast<const State&>(*ctx->state());
    switch (state.options.round_mode) {
      ROUND_CASE(DOWN)
      ROUND_CASE(UP)
      ROUND_CASE(TOWARDS_ZERO)
      ROUND_CASE(TOWARDS_INFINITY)
      ROUND_CASE(HALF_DOWN)
      ROUND_CASE(HALF_UP)
      ROUND_CASE(HALF_TOWARDS_ZERO)
      ROUND_CASE(HALF_TOWARDS_INFINITY)
      ROUND_CASE(HALF_TO_EVEN)
      ROUND_CASE(HALF_TO_ODD)
    }
    DCHECK(false);
    return Status::NotImplemented(
        "Internal implementation error: round mode not implemented: ",
        state.options.ToString());
  }
};
#undef ROUND_CASE

// Like MakeUnaryArithmeticFunction, but for unary rounding functions that control
// kernel dispatch based on RoundMode, only on non-null output.
template <template <typename, RoundMode, typename...> class Op, typename OptionsType>
std::shared_ptr<ScalarFunction> MakeUnaryRoundFunction(std::string name,
                                                       FunctionDoc doc) {
  using State = RoundOptionsWrapper<OptionsType>;
  static const OptionsType kDefaultOptions = OptionsType::Defaults();
  auto func = std::make_shared<ArithmeticIntegerToFloatingPointFunction>(
      name, Arity::Unary(), std::move(doc), &kDefaultOptions);
  for (const auto& ty : {float32(), float64(), decimal128(1, 0), decimal256(1, 0)}) {
    auto type_id = ty->id();
    ArrayKernelExec exec = nullptr;
    switch (type_id) {
      case Type::FLOAT:
        exec = RoundKernel<FloatType, OptionsType, Op>::Exec;
        break;
      case Type::DOUBLE:
        exec = RoundKernel<DoubleType, OptionsType, Op>::Exec;
        break;
      case Type::DECIMAL128:
        exec = RoundKernel<Decimal128Type, OptionsType, Op>::Exec;
        break;
      case Type::DECIMAL256:
        exec = RoundKernel<Decimal256Type, OptionsType, Op>::Exec;
        break;
      default:
        DCHECK(false);
        break;
    }
    DCHECK_OK(func->AddKernel(
        {InputType(type_id)},
        is_decimal(type_id) ? OutputType(FirstType) : OutputType(ty), exec, State::Init));
  }
  AddNullExec(func.get());
  return func;
}

// Like MakeUnaryArithmeticFunction, but for signed arithmetic ops that need to run
// only on non-null output.
template <typename Op>
std::shared_ptr<ScalarFunction> MakeUnarySignedArithmeticFunctionNotNull(
    std::string name, FunctionDoc doc) {
  auto func = std::make_shared<ArithmeticFunction>(name, Arity::Unary(), std::move(doc));
  for (const auto& ty : NumericTypes()) {
    if (!arrow::is_unsigned_integer(ty->id())) {
      auto exec = ArithmeticExecFromOp<ScalarUnaryNotNull, Op>(ty);
      DCHECK_OK(func->AddKernel({ty}, ty, exec));
    }
  }
  AddNullExec(func.get());
  return func;
}

template <typename Op>
std::shared_ptr<ScalarFunction> MakeBitWiseFunctionNotNull(std::string name,
                                                           FunctionDoc doc) {
  auto func = std::make_shared<ArithmeticFunction>(name, Arity::Binary(), std::move(doc));
  for (const auto& ty : IntTypes()) {
    auto exec = TypeAgnosticBitWiseExecFromOp<ScalarBinaryNotNullEqualTypes, Op>(ty);
    DCHECK_OK(func->AddKernel({ty, ty}, ty, exec));
  }
  AddNullExec(func.get());
  return func;
}

template <typename Op>
std::shared_ptr<ScalarFunction> MakeShiftFunctionNotNull(std::string name,
                                                         FunctionDoc doc) {
  auto func = std::make_shared<ArithmeticFunction>(name, Arity::Binary(), std::move(doc));
  for (const auto& ty : IntTypes()) {
    auto exec = ShiftExecFromOp<ScalarBinaryNotNullEqualTypes, Op>(ty);
    DCHECK_OK(func->AddKernel({ty, ty}, ty, exec));
  }
  AddNullExec(func.get());
  return func;
}

template <typename Op, typename FunctionImpl = ArithmeticFloatingPointFunction>
std::shared_ptr<ScalarFunction> MakeUnaryArithmeticFunctionFloatingPoint(
    std::string name, FunctionDoc doc) {
  auto func = std::make_shared<FunctionImpl>(name, Arity::Unary(), std::move(doc));
  for (const auto& ty : FloatingPointTypes()) {
    auto exec = GenerateArithmeticFloatingPoint<ScalarUnary, Op>(ty);
    DCHECK_OK(func->AddKernel({ty}, ty, exec));
  }
  AddNullExec(func.get());
  return func;
}

template <typename Op>
std::shared_ptr<ScalarFunction> MakeUnaryArithmeticFunctionFloatingPointNotNull(
    std::string name, FunctionDoc doc) {
  auto func = std::make_shared<ArithmeticFloatingPointFunction>(name, Arity::Unary(),
                                                                std::move(doc));
  for (const auto& ty : FloatingPointTypes()) {
    auto exec = GenerateArithmeticFloatingPoint<ScalarUnaryNotNull, Op>(ty);
    DCHECK_OK(func->AddKernel({ty}, ty, exec));
  }
  AddNullExec(func.get());
  return func;
}

template <typename Op>
std::shared_ptr<ScalarFunction> MakeArithmeticFunctionFloatingPoint(std::string name,
                                                                    FunctionDoc doc) {
  auto func = std::make_shared<ArithmeticFloatingPointFunction>(name, Arity::Binary(),
                                                                std::move(doc));
  for (const auto& ty : FloatingPointTypes()) {
    auto exec = GenerateArithmeticFloatingPoint<ScalarBinaryEqualTypes, Op>(ty);
    DCHECK_OK(func->AddKernel({ty, ty}, ty, exec));
  }
  AddNullExec(func.get());
  return func;
}

template <typename Op>
std::shared_ptr<ScalarFunction> MakeArithmeticFunctionFloatingPointNotNull(
    std::string name, FunctionDoc doc) {
  auto func = std::make_shared<ArithmeticFloatingPointFunction>(name, Arity::Binary(),
                                                                std::move(doc));
  for (const auto& ty : FloatingPointTypes()) {
    auto output = is_integer(ty->id()) ? float64() : ty;
    auto exec = GenerateArithmeticFloatingPoint<ScalarBinaryNotNullEqualTypes, Op>(ty);
    DCHECK_OK(func->AddKernel({ty, ty}, output, exec));
  }
  AddNullExec(func.get());
  return func;
}

template <template <int64_t> class Op>
void AddArithmeticFunctionTimeDuration(std::shared_ptr<ScalarFunction> func) {
  // Add Op(time32, duration) -> time32
  TimeUnit::type unit = TimeUnit::SECOND;
  auto exec_1 = ScalarBinary<Time32Type, Time32Type, DurationType, Op<86400>>::Exec;
  DCHECK_OK(func->AddKernel({time32(unit), duration(unit)}, OutputType(FirstType),
                            std::move(exec_1)));

  unit = TimeUnit::MILLI;
  auto exec_2 = ScalarBinary<Time32Type, Time32Type, DurationType, Op<86400000>>::Exec;
  DCHECK_OK(func->AddKernel({time32(unit), duration(unit)}, OutputType(FirstType),
                            std::move(exec_2)));

  // Add Op(time64, duration) -> time64
  unit = TimeUnit::MICRO;
  auto exec_3 = ScalarBinary<Time64Type, Time64Type, DurationType, Op<86400000000>>::Exec;
  DCHECK_OK(func->AddKernel({time64(unit), duration(unit)}, OutputType(FirstType),
                            std::move(exec_3)));

  unit = TimeUnit::NANO;
  auto exec_4 =
      ScalarBinary<Time64Type, Time64Type, DurationType, Op<86400000000000>>::Exec;
  DCHECK_OK(func->AddKernel({time64(unit), duration(unit)}, OutputType(FirstType),
                            std::move(exec_4)));
}

const FunctionDoc floor_doc{
    "Round down to the nearest integer",
    ("Compute the largest integer value not greater in magnitude than `x`."),
    {"x"}};

const FunctionDoc ceil_doc{
    "Round up to the nearest integer",
    ("Compute the smallest integer value not less in magnitude than `x`."),
    {"x"}};

const FunctionDoc trunc_doc{
    "Compute the integral part",
    ("Compute the nearest integer not greater in magnitude than `x`."),
    {"x"}};

const FunctionDoc round_doc{
    "Round to a given precision",
    ("Options are used to control the number of digits and rounding mode.\n"
     "Default behavior is to round to the nearest integer and\n"
     "use half-to-even rule to break ties."),
    {"x"},
    "RoundOptions"};

const FunctionDoc round_to_multiple_doc{
    "Round to a given multiple",
    ("Options are used to control the rounding multiple and rounding mode.\n"
     "Default behavior is to round to the nearest integer and\n"
     "use half-to-even rule to break ties."),
    {"x"},
    "RoundToMultipleOptions"};
}  // namespace

void RegisterScalarRoundArithmetic(FunctionRegistry* registry) {
  auto floor =
      MakeUnaryArithmeticFunctionFloatingPoint<Floor,
                                               ArithmeticIntegerToFloatingPointFunction>(
          "floor", floor_doc);
  DCHECK_OK(floor->AddKernel(
      {InputType(Type::DECIMAL128)}, OutputType(FirstType),
      FixedRoundDecimalExec<Decimal128Type, RoundMode::DOWN, /*ndigits=*/0>));
  DCHECK_OK(floor->AddKernel(
      {InputType(Type::DECIMAL256)}, OutputType(FirstType),
      FixedRoundDecimalExec<Decimal256Type, RoundMode::DOWN, /*ndigits=*/0>));
  DCHECK_OK(registry->AddFunction(std::move(floor)));

  auto ceil =
      MakeUnaryArithmeticFunctionFloatingPoint<Ceil,
                                               ArithmeticIntegerToFloatingPointFunction>(
          "ceil", ceil_doc);
  DCHECK_OK(ceil->AddKernel(
      {InputType(Type::DECIMAL128)}, OutputType(FirstType),
      FixedRoundDecimalExec<Decimal128Type, RoundMode::UP, /*ndigits=*/0>));
  DCHECK_OK(ceil->AddKernel(
      {InputType(Type::DECIMAL256)}, OutputType(FirstType),
      FixedRoundDecimalExec<Decimal256Type, RoundMode::UP, /*ndigits=*/0>));
  DCHECK_OK(registry->AddFunction(std::move(ceil)));

  auto trunc =
      MakeUnaryArithmeticFunctionFloatingPoint<Trunc,
                                               ArithmeticIntegerToFloatingPointFunction>(
          "trunc", trunc_doc);
  DCHECK_OK(trunc->AddKernel(
      {InputType(Type::DECIMAL128)}, OutputType(FirstType),
      FixedRoundDecimalExec<Decimal128Type, RoundMode::TOWARDS_ZERO, /*ndigits=*/0>));
  DCHECK_OK(trunc->AddKernel(
      {InputType(Type::DECIMAL256)}, OutputType(FirstType),
      FixedRoundDecimalExec<Decimal256Type, RoundMode::TOWARDS_ZERO, /*ndigits=*/0>));
  DCHECK_OK(registry->AddFunction(std::move(trunc)));

  auto round = MakeUnaryRoundFunction<Round, RoundOptions>("round", round_doc);
  DCHECK_OK(registry->AddFunction(std::move(round)));

  auto round_to_multiple =
      MakeUnaryRoundFunction<RoundToMultiple, RoundToMultipleOptions>(
          "round_to_multiple", round_to_multiple_doc);
  DCHECK_OK(registry->AddFunction(std::move(round_to_multiple)));
}

}  // namespace internal
}  // namespace compute
}  // namespace arrow
