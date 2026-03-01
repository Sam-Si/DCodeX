# Copyright 2024 DCodeX Team
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Language detection for DCodeX Python Client."""

from pathlib import Path


class LanguageDetector:
    """Detects programming language from file paths and directories."""

    @staticmethod
    def detect_from_file(file_path: Path) -> str:
        """Detect execution language from file extension.

        Args:
            file_path: Path to the source file.

        Returns:
            Language string for the server ("c", "cpp", or "python").
        """
        suffix = file_path.suffix.lower()
        if suffix == ".py":
            return "python"
        if suffix == ".c":
            return "c"
        return "cpp"

    @staticmethod
    def detect_from_directory(directory: Path) -> str:
        """Detect execution language from directory name.

        The function checks if the directory name contains 'python' or 'c' to determine
        the language. Otherwise, it defaults to 'cpp'.

        Args:
            directory: Path to the directory containing code files.

        Returns:
            Language string for the server ("c", "cpp", or "python").
        """
        dir_name = directory.name.lower()
        if "python" in dir_name:
            return "python"
        if dir_name == "c":
            return "c"
        return "cpp"

    @staticmethod
    def get_extensions(language: str) -> list[str]:
        """Get file extensions for a given language.

        Args:
            language: Language string ("c", "cpp", or "python").

        Returns:
            List of file extensions for the language.
        """
        if language == "python":
            return [".py"]
        elif language == "c":
            return [".c"]
        else:  # cpp
            return [".cpp", ".cc", ".cxx"]
