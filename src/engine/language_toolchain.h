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

#ifndef SRC_ENGINE_LANGUAGE_TOOLCHAIN_H_
#define SRC_ENGINE_LANGUAGE_TOOLCHAIN_H_

#include <memory>
#include <string>
#include <vector>

#include "absl/strings/string_view.h"

namespace dcodex {

// -----------------------------------------------------------------------------
// LanguageToolchainFactory: Abstract Factory Pattern (GoF)
// Decouples language logic from specific compiler/interpreter tools.
// Provides toolchain configuration for different programming languages.
// -----------------------------------------------------------------------------
class LanguageToolchainFactory {
 public:
  virtual ~LanguageToolchainFactory() = default;

  // Returns the file extension for this language (e.g., ".c", ".cpp", ".py").
  [[nodiscard]] virtual absl::string_view GetFileExtension() const = 0;

  // Returns the compiler/interpreter executable name (e.g., "clang", "python").
  [[nodiscard]] virtual absl::string_view GetExecutable() const = 0;

  // Returns the standard flags for compilation (e.g., "-std=c17", "-Wall").
  // Returns empty vector for interpreted languages.
  [[nodiscard]] virtual std::vector<std::string> GetStandardFlags() const = 0;

  // Returns a unique identifier for this language toolchain.
  [[nodiscard]] virtual absl::string_view GetLanguageId() const = 0;

  // Returns true if this language requires compilation before execution.
  [[nodiscard]] virtual bool RequiresCompilation() const = 0;

  // Factory method to create C toolchain.
  [[nodiscard]] static std::unique_ptr<LanguageToolchainFactory> CreateC();

  // Factory method to create C++ toolchain.
  [[nodiscard]] static std::unique_ptr<LanguageToolchainFactory> CreateCpp();

  // Factory method to create Python toolchain.
  [[nodiscard]] static std::unique_ptr<LanguageToolchainFactory> CreatePython();

  // Factory method to create toolchain based on file extension or language name.
  [[nodiscard]] static std::unique_ptr<LanguageToolchainFactory> Create(
      absl::string_view filename_or_extension);
};

// -----------------------------------------------------------------------------
// CToolchain: Concrete implementation for C language.
// Uses clang as the default compiler with C17 standard.
// -----------------------------------------------------------------------------
class CToolchain final : public LanguageToolchainFactory {
 public:
  CToolchain() = default;
  ~CToolchain() override = default;

  [[nodiscard]] absl::string_view GetFileExtension() const override { return ".c"; }
  [[nodiscard]] absl::string_view GetExecutable() const override { return "clang"; }
  [[nodiscard]] std::vector<std::string> GetStandardFlags() const override;
  [[nodiscard]] absl::string_view GetLanguageId() const override { return "c"; }
  [[nodiscard]] bool RequiresCompilation() const override { return true; }
};

// -----------------------------------------------------------------------------
// CppToolchain: Concrete implementation for C++ language.
// Uses clang++ as the default compiler with C++17 standard.
// -----------------------------------------------------------------------------
class CppToolchain final : public LanguageToolchainFactory {
 public:
  CppToolchain() = default;
  ~CppToolchain() override = default;

  [[nodiscard]] absl::string_view GetFileExtension() const override { return ".cpp"; }
  [[nodiscard]] absl::string_view GetExecutable() const override { return "clang++"; }
  [[nodiscard]] std::vector<std::string> GetStandardFlags() const override;
  [[nodiscard]] absl::string_view GetLanguageId() const override { return "cpp"; }
  [[nodiscard]] bool RequiresCompilation() const override { return true; }
};

// -----------------------------------------------------------------------------
// PythonToolchain: Concrete implementation for Python language.
// Uses python interpreter directly (no compilation required).
// -----------------------------------------------------------------------------
class PythonToolchain final : public LanguageToolchainFactory {
 public:
  PythonToolchain() = default;
  ~PythonToolchain() override = default;

  [[nodiscard]] absl::string_view GetFileExtension() const override { return ".py"; }
  [[nodiscard]] absl::string_view GetExecutable() const override { return "python"; }
  [[nodiscard]] std::vector<std::string> GetStandardFlags() const override;
  [[nodiscard]] absl::string_view GetLanguageId() const override { return "python"; }
  [[nodiscard]] bool RequiresCompilation() const override { return false; }
};

}  // namespace dcodex

#endif  // SRC_ENGINE_LANGUAGE_TOOLCHAIN_H_