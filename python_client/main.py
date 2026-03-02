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

"""Deprecated client entry point. Use python_client/main.py instead."""

import warnings

from python_client.main import main

if __name__ == "__main__":
    warnings.warn(
        "python_client/client.py is deprecated and will be removed in a future version. "
        "Please use python_client/main.py instead.",
        DeprecationWarning,
        stacklevel=2
    )
    main()
