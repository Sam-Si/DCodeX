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

"""Result printing for DCodeX Python Client."""

from python_client.execution_types import ExecutionResult
from python_client.formatter import (
    COLOR_BLUE,
    COLOR_BOLD,
    COLOR_CYAN,
    COLOR_GREEN,
    COLOR_MAGENTA,
    COLOR_RED,
    COLOR_RESET,
    COLOR_WHITE,
    COLOR_YELLOW,
    format_bytes,
    format_duration,
)


class ResultPrinter:
    """Prints execution results in a formatted way."""

    def print_results(self, results: ExecutionResult, show_output: bool = False) -> None:
        """Print execution results in a formatted way.

        Args:
            results: ExecutionResult to print.
            show_output: Whether to show stdout/stderr output. Defaults to False
                as output is now streamed.
        """
        if show_output:
            if results.stdout:
                print(results.stdout, end="")
            if results.stderr:
                # Add newline before STDERR if stdout didn't end with one
                if results.stdout and not results.stdout.endswith("\n"):
                    print()
                print(f"{COLOR_RED}STDERR: {results.stderr}{COLOR_RESET}", end="")

            if (
                    not results.stdout
                    and not results.stderr
                    and results.execution_time > 0
            ):
                # Execution finished but produced no output
                pass

        print("-" * 50)
        print(f"{COLOR_CYAN}{COLOR_BOLD}📊 Resource Usage Summary:{COLOR_RESET}")
        print(
            f"   💾 {COLOR_BOLD}Peak Memory:{COLOR_RESET} "
            f"{COLOR_MAGENTA}{format_bytes(results.peak_memory)}{COLOR_RESET}"
        )
        print(
            f"   ⏱️  {COLOR_BOLD}Execution Time:{COLOR_RESET} "
            f"{COLOR_GREEN}{format_duration(results.execution_time)}{COLOR_RESET}"
        )
        print(
            f"   🌐 {COLOR_BOLD}Network Time:{COLOR_RESET} "
            f"{COLOR_BLUE}{format_duration(results.actual_time)}{COLOR_RESET}"
        )
        cache_status = (
            f"{COLOR_YELLOW}⚡ CACHE HIT{COLOR_RESET}"
            if results.cache_hit
            else f"{COLOR_WHITE}🆕 Fresh Execution{COLOR_RESET}"
        )
        print(f"   {cache_status}")
        if results.cache_speedup is not None:
            print(
                f"   {COLOR_CYAN}⚡ Cache Speedup: "
                f"{COLOR_BOLD}{results.cache_speedup:.2f}x faster{COLOR_RESET}"
            )
        if results.wall_clock_timeout:
            print(
                f"   {COLOR_RED}{COLOR_BOLD}⏰ WALL-CLOCK TIMEOUT "
                "(process killed by sandbox){COLOR_RESET}"
            )
        if results.output_truncated:
            print(
                f"   {COLOR_RED}✂️  OUTPUT TRUNCATED "
                "(exceeded 10 KB combined output limit){COLOR_RESET}"
            )
        print("=" * 50)
