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

"""File management for DCodeX Python Client."""

from pathlib import Path

from python_client.language_detector import LanguageDetector


class FileManager:
    """Manages file operations for code execution."""

    def __init__(self, language_detector: LanguageDetector | None = None) -> None:
        """Initialize FileManager.

        Args:
            language_detector: Optional LanguageDetector instance.
                If not provided, a default one will be created.
        """
        self._language_detector = language_detector or LanguageDetector()

    def read_code_from_file(self, file_path: Path) -> str:
        """Read source code from a file.

        Args:
            file_path: Path to the source file.

        Returns:
            Contents of the file as a string.

        Raises:
            FileNotFoundError: If the file does not exist.
            OSError: If the file cannot be read.
        """
        with open(file_path, "r", encoding="utf-8") as f:
            return f.read()

    def read_codes_from_directory(
            self, directory: Path
    ) -> dict[str, tuple[str, str]]:
        """Read all code files from a directory.

        Language is automatically detected from the directory name.
        Files are returned with their detected language.

        Args:
            directory: Path to the directory containing code files.

        Returns:
            Dictionary mapping file names to tuples of (code, language).

        Raises:
            FileNotFoundError: If the directory does not exist.
        """
        codes: dict[str, tuple[str, str]] = {}
        if not directory.exists():
            raise FileNotFoundError(f"Directory not found: {directory}")

        language = self._language_detector.detect_from_directory(directory)
        extensions = self._language_detector.get_extensions(language)

        for ext in extensions:
            for file_path in directory.glob(f"*{ext}"):
                try:
                    code = self.read_code_from_file(file_path)
                    codes[file_path.stem] = (code, language)
                except OSError as e:
                    print(f"Warning: Could not read {file_path}: {e}")

        return codes
