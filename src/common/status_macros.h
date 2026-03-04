// Copyright 2024 DCodeX Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef SRC_COMMON_STATUS_MACROS_H_
#define SRC_COMMON_STATUS_MACROS_H_

#include "absl/status/status.h"
#include "absl/status/statusor.h"

#define DCODEX_STATUS_MACROS_CONCAT_NAME_INNER(x, y) x##y
#define DCODEX_STATUS_MACROS_CONCAT_NAME(x, y) \
  DCODEX_STATUS_MACROS_CONCAT_NAME_INNER(x, y)

#define ABSL_RETURN_IF_ERROR(expr)                                           \
  do {                                                                       \
    const absl::Status _status = (expr);                                     \
    if (ABSL_PREDICT_FALSE(!_status.ok())) return _status;                   \
  } while (0)

#define ABSL_ASSIGN_OR_RETURN(lhs, rexpr)                                    \
  ABSL_ASSIGN_OR_RETURN_IMPL(                                                \
      DCODEX_STATUS_MACROS_CONCAT_NAME(_status_or_value, __LINE__), lhs, rexpr)

#define ABSL_ASSIGN_OR_RETURN_IMPL(statusor, lhs, rexpr)                     \
  auto statusor = (rexpr);                                                   \
  if (ABSL_PREDICT_FALSE(!statusor.ok())) {                                  \
    return statusor.status();                                                \
  }                                                                          \
  lhs = std::move(statusor).value()

#endif  // SRC_COMMON_STATUS_MACROS_H_
