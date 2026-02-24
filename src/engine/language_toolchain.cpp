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

#include "src/engine/language_toolchain.h"

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/match.h"
#include "absl/strings/string_view.h"

namespace dcodex {

// -----------------------------------------------------------------------------
// CToolchain Implementation
// -----------------------------------------------------------------------------

std::vector<std::string> CToolchain::GetStandardFlags() const {
  return {"-std=c11", "-Wall"};
}

// -----------------------------------------------------------------------------
// CppToolchain Implementation
// -----------------------------------------------------------------------------

std::vector<std::string> CppToolchain::GetStandardFlags() const {
  return {"-std=c++17"};
}

// -----------------------------------------------------------------------------
// PythonToolchain Implementation
// -----------------------------------------------------------------------------

std::vector<std::string> PythonToolchain::GetStandardFlags() const {
  return {"-u"};  // Unbuffered output for real-time streaming
}

// -----------------------------------------------------------------------------
// LanguageToolchainFactory Factory Methods
// -----------------------------------------------------------------------------

std::unique_ptr<LanguageToolchainFactory> LanguageToolchainFactory::CreateC() {
  return std::make_unique<CToolchain>();
}

std::unique_ptr<LanguageToolchainFactory> LanguageToolchainFactory::CreateCpp() {
  return std::make_unique<CppToolchain>();
}

std::unique_ptr<LanguageToolchainFactory> LanguageToolchainFactory::CreatePython() {
  return std::make_unique<PythonToolchain>();
}

std::unique_ptr<LanguageToolchainFactory> LanguageToolchainFactory::Create(
    absl::string_view filename_or_extension) {
  // Check for Python
  if (absl::EndsWith(filename_or_extension, ".py") ||
      filename_or_extension == "python") {
    return CreatePython();
  }

  // Check for C
  if (absl::EndsWith(filename_or_extension, ".c") ||
      filename_or_extension == "c") {
    return CreateC();
  }

  // Default to C++ for .cpp, .cc, .cxx, or unknown
  return CreateCpp();
}

}  // namespace dcodex