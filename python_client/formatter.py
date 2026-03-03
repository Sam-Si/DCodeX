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

"""Formatting utilities for DCodeX Python Client."""

from typing import Final

# Constants
_KILOBYTE: Final[int] = 1024
_MEGABYTE: Final[int] = 1024 * 1024
_GIGABYTE: Final[int] = 1024 * 1024 * 1024
_MILLISECOND: Final[int] = 1000
_MINUTE_MS: Final[int] = 60000

# ANSI Color Codes
COLOR_RESET: Final[str] = "\033[0m"
COLOR_BOLD: Final[str] = "\033[1m"
COLOR_RED: Final[str] = "\033[91m"
COLOR_GREEN: Final[str] = "\033[92m"
COLOR_YELLOW: Final[str] = "\033[93m"
COLOR_BLUE: Final[str] = "\033[94m"
COLOR_MAGENTA: Final[str] = "\033[95m"
COLOR_CYAN: Final[str] = "\033[96m"
COLOR_WHITE: Final[str] = "\033[97m"


def format_bytes(bytes_val: int) -> str:
    """Convert bytes to human-readable format.

    Args:
        bytes_val: Number of bytes.

    Returns:
        Human-readable string representation (B, KB, MB, or GB).
    """
    if bytes_val < _KILOBYTE:
        return f"{bytes_val} B"
    if bytes_val < _MEGABYTE:
        return f"{bytes_val / _KILOBYTE:.2f} KB"
    if bytes_val < _GIGABYTE:
        return f"{bytes_val / _MEGABYTE:.2f} MB"
    return f"{bytes_val / _GIGABYTE:.2f} GB"


def format_duration(ms: float) -> str:
    """Convert milliseconds to human-readable format.

    Args:
        ms: Duration in milliseconds.

    Returns:
        Human-readable string representation (ms, s, or m:s).
    """
    if ms < _MILLISECOND:
        return f"{ms:.2f} ms"
    if ms < _MINUTE_MS:
        return f"{ms / _MILLISECOND:.2f} s"
    minutes = int(ms / _MINUTE_MS)
    seconds = (ms % _MINUTE_MS) / _MILLISECOND
    return f"{minutes}m {seconds:.2f}s"
