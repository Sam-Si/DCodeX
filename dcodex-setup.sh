#!/usr/bin/env bash
# =============================================================================
# dcodex-setup.sh — DCodeX build environment setup + launch
#
# Usage:
#   ./dcodex-setup.sh                 # setup + build + start server
#   ./dcodex-setup.sh --test          # setup + build + run tests (no server)
#   ./dcodex-setup.sh --build-only    # setup + build (no tests, no server)
#   SKIP_APT=1 ./dcodex-setup.sh      # skip apt install (already done)
#   SLIM_IMAGE=1 ./dcodex-setup.sh    # purge build tools after build (Docker)
#   NO_CLEAN=0 ./dcodex-setup.sh      # force bazel clean (rarely needed)
#
# Environment overrides:
#   REPO_DIR          — path to DCodeX repo         (default: /testbed/DCodeX)
#   BAZEL_DISK_CACHE  — bazel action cache dir      (default: .bazel/disk_cache)
#   BAZEL_REPO_CACHE  — bazel repository cache dir  (default: .bazel/repo_cache)
#   LLVM_VERSION      — LLVM version to install     (default: 19)
#   BAZEL_JOBS        — parallel build jobs         (default: nproc * 1.5)
#   SERVER_PORT, SERVER_CPU_LIMIT, SERVER_WALL_TIMEOUT, SERVER_MEM_LIMIT, SERVER_OUTPUT_LIMIT
# =============================================================================

set -euo pipefail

# ── Colour helpers ────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

info()  { echo -e "${CYAN}[INFO]${NC}  $*"; }
ok()    { echo -e "${GREEN}[ OK ]${NC}  $*"; }
warn()  { echo -e "${YELLOW}[WARN]${NC}  $*"; }
die()   { echo -e "${RED}[FAIL]${NC}  $*" >&2; exit 1; }
step()  { echo -e "\n${BOLD}${CYAN}━━━  $*  ━━━${NC}"; }
timer() { echo -e "${YELLOW}  ⏱  Elapsed: $(( $(date +%s) - SCRIPT_START ))s${NC}"; }

SCRIPT_START=$(date +%s)

# ── Parse flags ───────────────────────────────────────────────────────────────
MODE="server"          # server | test | build-only
for arg in "$@"; do
  case "$arg" in
    --test)       MODE="test" ;;
    --build-only) MODE="build-only" ;;
    --help|-h)
      sed -n '3,20p' "$0" | sed 's/^# \?//'
      exit 0 ;;
    *) die "Unknown argument: $arg" ;;
  esac
done

# ── Configuration ─────────────────────────────────────────────────────────────
REPO_DIR="${REPO_DIR:-/testbed/DCodeX}"
BAZEL_DISK_CACHE="${BAZEL_DISK_CACHE:-${REPO_DIR}/.bazel/disk_cache}"
BAZEL_REPO_CACHE="${BAZEL_REPO_CACHE:-${REPO_DIR}/.bazel/repo_cache}"
LLVM_VERSION="${LLVM_VERSION:-19}"
SKIP_APT="${SKIP_APT:-0}"
SLIM_IMAGE="${SLIM_IMAGE:-0}"   # 1 = purge build tools (good for Docker final image)
NO_CLEAN="${NO_CLEAN:-1}"       # 1 = skip bazel clean (STRONGLY recommended)

# Server flags — tuned for production testing.
#   CPU limit:    5s — enough for clang compilation + moderate loops.
#   Wall timeout: 5s — kills truly-infinite loops promptly.
#   Memory limit: 100MB — enough for normal programs; memory-exhaustion
#                 examples (50MB/chunk) fail after 1-2 allocations.
#   Output limit: 64KB — accommodates verbose examples (file_operations ~2KB);
#                 catches runaway output (100KB+ triggers truncation).
SERVER_PORT="${SERVER_PORT:-50051}"
SERVER_CPU_LIMIT="${SERVER_CPU_LIMIT:-5}"
SERVER_WALL_TIMEOUT="${SERVER_WALL_TIMEOUT:-5}"
SERVER_MEM_LIMIT="${SERVER_MEM_LIMIT:-104857600}"   # 100 MB
SERVER_OUTPUT_LIMIT="${SERVER_OUTPUT_LIMIT:-65536}"  # 64 KB

# Compute job count: nproc * 1.5, round up, never less than 4
NCPUS=$(nproc 2>/dev/null || echo 4)
BAZEL_JOBS="${BAZEL_JOBS:-$(( (NCPUS * 3 + 1) / 2 ))}"
# Cap JVM Heap at 8GB (8192MB) explicitly. Bazel's AST parser does not 
# need 32GB on a 64GB node; leaving RAM available for Clang/LLD
BAZEL_MEM_MB="${BAZEL_MEM_MB:-8192}"

# ── Detect Ubuntu codename for LLVM apt repo ─────────────────────────────────
UBUNTU_CODENAME=""
if [[ -f /etc/os-release ]]; then
  # shellcheck disable=SC1091
  source /etc/os-release
  UBUNTU_CODENAME="${VERSION_CODENAME:-}"
fi
UBUNTU_CODENAME="${UBUNTU_CODENAME:-noble}"  # default to 24.04 LTS

echo -e "${BOLD}"
echo "  ██████╗  ██████╗ ██████╗ ██████╗ ███████╗██╗  ██╗"
echo "  ██╔══██╗██╔════╝██╔═══██╗██╔══██╗██╔════╝╚██╗██╔╝"
echo "  ██║  ██║██║     ██║   ██║██║  ██║█████╗   ╚███╔╝ "
echo "  ██║  ██║██║     ██║   ██║██║  ██║██╔══╝   ██╔██╗ "
echo "  ██████╔╝╚██████╗╚██████╔╝██████╔╝███████╗██╔╝ ██╗"
echo "  ╚═════╝  ╚═════╝ ╚═════╝ ╚═════╝ ╚══════╝╚═╝  ╚═╝"
echo -e "${NC}"
info "Repo:        ${REPO_DIR}"
info "Mode:        ${MODE}"
info "CPUs / jobs: ${NCPUS} / ${BAZEL_JOBS}"
info "Bazel mem:   ${BAZEL_MEM_MB} MB JVM heap"
info "Disk cache:  ${BAZEL_DISK_CACHE}"
info "Repo cache:  ${BAZEL_REPO_CACHE}"
info "LLVM:        ${LLVM_VERSION} (Ubuntu ${UBUNTU_CODENAME})"

# ── Helpers ───────────────────────────────────────────────────────────────────
require_root() {
  [[ $EUID -eq 0 ]] || die "This script must be run as root (or via sudo)"
}

cmd_exists() { command -v "$1" &>/dev/null; }

version_gte() {
  # Returns 0 (true) if $1 >= $2 (semver compare)
  [[ "$(printf '%s\n%s' "$2" "$1" | sort -V | head -n1)" == "$2" ]]
}

# Remove stale sandbox working directories.  Leftover sandbox state from
# a previous build/test (Ctrl+C, OOM kill, crash, or Bazel's own async
# cleanup not finishing in time) causes "Could not copy inputs into
# sandbox … (File exists)" on the next run.  This is cheap (~instant)
# and only removes sandbox working dirs — disk cache & repo cache are
# untouched.  Called once at startup AND before every `bazel test`
# invocation so no stale state ever leaks across sanitizer suites.
purge_sandbox_dirs() {
  local sandbox_dir="${REPO_DIR}/.bazel/output_base/sandbox"
  if [[ -d "$sandbox_dir" ]]; then
    rm -rf "$sandbox_dir"
  fi
}

# ─────────────────────────────────────────────────────────────────────────────
# STEP 1 — Pre-flight checks
# ─────────────────────────────────────────────────────────────────────────────
step "1/7  Pre-flight checks"

require_root

# OS must be Linux
[[ "$(uname -s)" == "Linux" ]] || die "This script is Linux-only"

# Architecture
ARCH=$(uname -m)
case "$ARCH" in
  x86_64)  BAZEL_ARCH="amd64" ;;
  aarch64) BAZEL_ARCH="arm64" ;;
  *)        die "Unsupported architecture: ${ARCH}" ;;
esac
info "Architecture: ${ARCH} (Bazel: ${BAZEL_ARCH})"

# glibc version — need ≥ 2.34 for close_range syscall wrapper in glibc
# (We use the raw SYS_close_range syscall anyway, so this is a soft check)
GLIBC_VER=$(ldd --version 2>/dev/null | head -n1 | grep -oE '[0-9]+\.[0-9]+' | head -n1 || echo "0.0")
if version_gte "$GLIBC_VER" "2.34"; then
  ok "glibc ${GLIBC_VER} — close_range() available natively"
elif version_gte "$GLIBC_VER" "2.17"; then
  warn "glibc ${GLIBC_VER} — will use SYS_close_range syscall fallback (kernel ≥ 5.9 required)"
else
  die "glibc ${GLIBC_VER} is too old — need ≥ 2.17"
fi

# Kernel version for close_range (kernel ≥ 5.9 adds SYS_close_range)
KERNEL_VER=$(uname -r | grep -oE '^[0-9]+\.[0-9]+')
if version_gte "$KERNEL_VER" "5.9"; then
  ok "Kernel ${KERNEL_VER} — SYS_close_range supported"
else
  warn "Kernel ${KERNEL_VER} — SYS_close_range NOT available; /proc/self/fd fallback will be used"
fi

# Repo directory
[[ -d "${REPO_DIR}" ]] || die "REPO_DIR not found: ${REPO_DIR}"
[[ -f "${REPO_DIR}/MODULE.bazel" ]] || die "Not a DCodeX repo: ${REPO_DIR}/MODULE.bazel missing"
ok "Repo found at ${REPO_DIR}"

# ─────────────────────────────────────────────────────────────────────────────
# STEP 2 — System dependencies
# ─────────────────────────────────────────────────────────────────────────────
step "2/7  System dependencies"

if [[ "$SKIP_APT" == "1" ]]; then
  warn "SKIP_APT=1 — skipping apt install"
else
  info "Updating apt and installing core packages..."
  # Avoid tzdata interactive prompt
  DEBIAN_FRONTEND=noninteractive apt-get update -qq
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    wget curl gnupg ca-certificates ca-certificates-java git \
    python3 python3-pip python3-venv python-is-python3 \
    libexpat1 libsqlite3-0 zlib1g \
    binutils-gold \
    2>/dev/null
  ok "Core packages installed"

  # ── Java truststore refresh ──────────────────────────────────────────────
  # Bazel's embedded JVM uses /etc/ssl/certs/java/cacerts for TLS.
  # When ca-certificates is upgraded (e.g. 20240203 → 20260601) the PEM
  # bundle is rebuilt, but the Java keystore is only updated if
  # ca-certificates-java's update hook runs *after* the new PEMs land.
  # In Docker/CI the hook sometimes fires before the PEMs are fully in
  # place, leaving a stale keystore → "PKIX path building failed" when
  # Bazel fetches from bcr.bazel.build.  Force-regenerate it now.
  info "Refreshing Java certificate truststore..."
  if [[ -x /usr/sbin/update-ca-certificates ]]; then
    update-ca-certificates -f 2>/dev/null || true
  fi
  # Directly invoke the Java keystore hook if available (belt-and-suspenders).
  if [[ -x /etc/ca-certificates/update.d/jks-keystore ]]; then
    /etc/ca-certificates/update.d/jks-keystore 2>/dev/null || true
  fi
  # Verify the keystore exists.
  if [[ -f /etc/ssl/certs/java/cacerts ]]; then
    ok "Java truststore refreshed: /etc/ssl/certs/java/cacerts"
  else
    warn "Java truststore not found at /etc/ssl/certs/java/cacerts — Bazel registry access may fail"
  fi

  # ── LLVM (clang + lld) ────────────────────────────────────────────────────
  if cmd_exists "clang-${LLVM_VERSION}" && cmd_exists "lld-${LLVM_VERSION}"; then
    ok "LLVM ${LLVM_VERSION} already present — skipping apt install"
  else
    info "Setting up LLVM ${LLVM_VERSION} apt repository (Ubuntu ${UBUNTU_CODENAME})..."
    mkdir -p /usr/share/keyrings
    curl -fsSL https://apt.llvm.org/llvm-snapshot.gpg.key \
      | gpg --dearmor -o /usr/share/keyrings/llvm-archive-keyring.gpg
    chmod 644 /usr/share/keyrings/llvm-archive-keyring.gpg
    echo "deb [signed-by=/usr/share/keyrings/llvm-archive-keyring.gpg] \
http://apt.llvm.org/${UBUNTU_CODENAME}/ llvm-toolchain-${UBUNTU_CODENAME}-${LLVM_VERSION} main" \
      > /etc/apt/sources.list.d/llvm.list
    DEBIAN_FRONTEND=noninteractive apt-get update -qq
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
      "clang-${LLVM_VERSION}" \
      "lld-${LLVM_VERSION}" \
      "libc++-${LLVM_VERSION}-dev" \
      "libc++abi-${LLVM_VERSION}-dev" \
      2>/dev/null
    ok "LLVM ${LLVM_VERSION} installed (includes libc++ for MSan)"
  fi

  # ── Sanitizer runtime headers (needed for --config=asan/msan/tsan) ───────
  # The sanitizer headers (sanitizer/asan_interface.h, sanitizer/lsan_interface.h)
  # are NOT shipped with the base clang package. Without them, abseil-cpp and
  # gRPC fail to compile under any sanitizer configuration.
  info "Ensuring sanitizer runtime headers are installed..."
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    "libclang-rt-${LLVM_VERSION}-dev" \
    2>/dev/null \
    || warn "libclang-rt-${LLVM_VERSION}-dev not available — sanitizer builds may fail"
  ok "Sanitizer runtime headers installed"

  # ── libc++ (required for MSan — see .bazelrc msan config) ────────────
  # MSan needs -stdlib=libc++ because its runtime has interceptors for
  # libc++ but not libstdc++.  Without libc++, <cstdint> and other
  # standard headers are missing and compilation fails.
  info "Ensuring libc++ is installed (required for MSan)..."
  DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
    "libc++-${LLVM_VERSION}-dev" \
    "libc++abi-${LLVM_VERSION}-dev" \
    2>/dev/null \
    || warn "libc++ packages not available — MSan builds will fail"
  ok "libc++ installed"

  # ── Symlinks ─────────────────────────────────────────────────────────────
  info "Creating LLVM symlinks..."
  ln -sf "/usr/bin/clang-${LLVM_VERSION}"   /usr/bin/clang
  ln -sf "/usr/bin/clang++-${LLVM_VERSION}" /usr/bin/clang++
  ln -sf "/usr/bin/lld-${LLVM_VERSION}"     /usr/bin/lld
  ln -sf "/usr/bin/lld-${LLVM_VERSION}"     /usr/bin/ld.lld
  # clangd optional (IDE support)
  if [[ -f "/usr/bin/clangd-${LLVM_VERSION}" ]]; then
    ln -sf "/usr/bin/clangd-${LLVM_VERSION}" /usr/bin/clangd
  fi
  ok "Symlinks: clang → clang-${LLVM_VERSION}, lld → lld-${LLVM_VERSION}"
fi

# Validate toolchain
clang --version | head -n1 | grep -qE "clang version ${LLVM_VERSION}" \
  || warn "clang symlink might not point to LLVM ${LLVM_VERSION} — check manually"
ld.lld --version 2>&1 | head -n1 | grep -qi lld \
  || die "ld.lld not found or not working"
ok "Toolchain check: clang=$(clang --version | head -n1 | grep -oE 'clang version [0-9.]+'), lld=$(ld.lld --version 2>&1 | head -n1)"

# ── Bazelisk ──────────────────────────────────────────────────────────────────
if cmd_exists bazel; then
  ok "bazel/bazelisk already in PATH: $(bazel version --gnu_format 2>/dev/null | grep -oE 'Build label: .*' || echo "(bazelisk)")"
else
  info "Installing bazelisk as 'bazel'..."
  curl -fsSL \
    "https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-${BAZEL_ARCH}" \
    -o /usr/local/bin/bazel
  chmod +x /usr/local/bin/bazel
  ok "bazelisk installed at /usr/local/bin/bazel"
fi

if [[ ! -f "${REPO_DIR}/bazelisk" ]]; then
  info "Downloading local copy of bazelisk into ${REPO_DIR}/bazelisk..."
  curl -fsSL \
    "https://github.com/bazelbuild/bazelisk/releases/latest/download/bazelisk-linux-${BAZEL_ARCH}" \
    -o "${REPO_DIR}/bazelisk"
  chmod +x "${REPO_DIR}/bazelisk"
fi

timer

# ─────────────────────────────────────────────────────────────────────────────
# STEP 3 — Python + gRPC tooling
# ─────────────────────────────────────────────────────────────────────────────
step "3/7  Python & gRPC tooling"

info "Installing grpcurl..."
if cmd_exists grpcurl; then
  ok "grpcurl already installed"
else
  GRPCURL_ARCH="x86_64"
  [[ "$ARCH" == "aarch64" ]] && GRPCURL_ARCH="arm64"
  curl -fsSL "https://github.com/fullstorydev/grpcurl/releases/download/v1.9.3/grpcurl_1.9.3_linux_${GRPCURL_ARCH}.tar.gz" | tar -xz -C /usr/local/bin grpcurl
  chmod +x /usr/local/bin/grpcurl
  ok "grpcurl installed at /usr/local/bin/grpcurl"
fi

if [[ ! -f "${REPO_DIR}/bin/grpcurl" ]]; then
  info "Downloading local copy of grpcurl into ${REPO_DIR}/bin/grpcurl..."
  mkdir -p "${REPO_DIR}/bin"
  GRPCURL_ARCH="x86_64"
  [[ "$ARCH" == "aarch64" ]] && GRPCURL_ARCH="arm64"
  curl -fsSL "https://github.com/fullstorydev/grpcurl/releases/download/v1.9.3/grpcurl_1.9.3_linux_${GRPCURL_ARCH}.tar.gz" | tar -xz -C "${REPO_DIR}/bin" grpcurl
  chmod +x "${REPO_DIR}/bin/grpcurl"
fi

info "Installing grpcio-tools and client requirements..."
# --break-system-packages: Ubuntu 24.04+ marks system Python as EXTERNALLY-MANAGED.
# Since we're running as root in CI/Docker, venvs are unnecessary overhead.
# We skip `pip install --upgrade pip` because the apt-managed pip cannot be
# upgraded via pip itself (RECORD file not found).
python -m pip install --quiet --break-system-packages --ignore-installed \
  grpcio-tools \
  -r "${REPO_DIR}/python_client/requirements.txt"

info "Generating Python gRPC bindings from proto/sandbox.proto..."
cd "${REPO_DIR}"
python -m grpc_tools.protoc \
  -I. \
  --python_out=. \
  --grpc_python_out=. \
  proto/sandbox.proto
ok "proto/sandbox_pb2.py and proto/sandbox_pb2_grpc.py generated"

# ─────────────────────────────────────────────────────────────────────────────
# STEP 4 — Bazel cache directories
# ─────────────────────────────────────────────────────────────────────────────
step "4/7  Cache setup"

mkdir -p "${BAZEL_DISK_CACHE}" "${BAZEL_REPO_CACHE}"
info "Disk cache:  ${BAZEL_DISK_CACHE} ($(du -sh "${BAZEL_DISK_CACHE}" 2>/dev/null | cut -f1 || echo "empty") used)"
info "Repo cache:  ${BAZEL_REPO_CACHE} ($(du -sh "${BAZEL_REPO_CACHE}" 2>/dev/null | cut -f1 || echo "empty") used)"

# Critically: do NOT run 'bazel clean' — it nukes the disk cache and turns a
# 5-second incremental build into a 3-minute cold build. Only clean explicitly.
if [[ "${NO_CLEAN}" == "0" ]]; then
  warn "NO_CLEAN=0 — running bazel clean (this destroys the disk cache!)"
  bazel clean --expunge
else
  ok "Skipping bazel clean (incremental build — disk cache preserved)"
fi

purge_sandbox_dirs
ok "Sandbox directories clean"

timer

# ─────────────────────────────────────────────────────────────────────────────
# STEP 5 — Build
# ─────────────────────────────────────────────────────────────────────────────
step "5/7  Bazel build  (${BAZEL_JOBS} jobs)"
cd "${REPO_DIR}"

# Override JVM heap for this machine's available memory.
# The .bazelrc has M1-tuned 16g; on a Linux CI runner that may OOM the JVM.
#
# javax.net.ssl.trustStore: Explicitly point Bazel's JVM at the system-managed
# Java keystore.  This is the belt-and-suspenders complement to the
# ca-certificates-java install + keystore refresh in Step 2.  Without it,
# the JVM may use a bundled (and potentially empty/stale) truststore,
# causing PKIX validation failures against bcr.bazel.build and other
# HTTPS registries.
JAVA_TRUSTSTORE="/etc/ssl/certs/java/cacerts"
BAZEL_JVM_FLAGS=(
  "--host_jvm_args=-Xmx${BAZEL_MEM_MB}m"
  "--host_jvm_args=-XX:+UseG1GC"
  "--host_jvm_args=-XX:MaxGCPauseMillis=50"
)
if [[ -f "${JAVA_TRUSTSTORE}" ]]; then
  BAZEL_JVM_FLAGS+=("--host_jvm_args=-Djavax.net.ssl.trustStore=${JAVA_TRUSTSTORE}")
fi

# Build targets. We build the test binary too so incremental test runs are fast.
BUILD_TARGETS=(
  "//src/api:server"
  "//src/engine:sandbox_test"
)

info "Targets: ${BUILD_TARGETS[*]}"
BUILD_START=$(date +%s)

bazel "${BAZEL_JVM_FLAGS[@]}" build \
  --jobs="${BAZEL_JOBS}" \
  --local_resources="cpu=${NCPUS}" \
  --local_resources="memory=${BAZEL_MEM_MB}" \
  "${BUILD_TARGETS[@]}"

BUILD_END=$(date +%s)
ok "Build complete in $(( BUILD_END - BUILD_START ))s"
timer

# ─────────────────────────────────────────────────────────────────────────────
# STEP 6 — Tests (optional)
# ─────────────────────────────────────────────────────────────────────────────
step "6/7  Tests"

# Common Bazel test flags for diagnostics — always verbose.
# NOTE: --sandbox_debug is intentionally omitted; it dumps per-action
# traces for every compile/link step, drowning test output. Pass it
# manually if debugging sandbox issues: bazel test --sandbox_debug ...
BAZEL_TEST_COMMON=(
  --verbose_failures
  --test_output=all
  --test_env=HOME=/tmp
)

# The specific test targets to run under sanitizers.
# We do NOT use //... because:
#   1. It picks up tests tagged for exclusion under certain sanitizers.
#   2. One test failure aborts the entire invocation (all others = NO STATUS).
#   3. MSan requires all linked libraries to be instrumented — system libstdc++ is not.
ENGINE_TESTS=(
  "//src/engine:sandbox_test"
  "//src/engine:warm_worker_pool_test"
  "//src/engine:dynamic_worker_coordinator_test"
  "//src/engine:tsan_checker"
)

# ── Helper: dump test logs on failure ─────────────────────────────────────
# Bazel buries the actual test output inside bazel-testlogs/<target>/test.log.
# This function finds all test.log files and prints their content so failures
# are diagnosable without manually navigating the output tree.
dump_test_logs() {
  local config_name="$1"
  local log_dir="${REPO_DIR}/bazel-testlogs"
  
  # The bazel-testlogs convenience symlink is the primary source.
  # Fall back to the output_base path used when --output_base is set,
  # and finally to the .bazel/testlogs path from dcodex-setup config.
  if [[ ! -d "$log_dir" ]]; then
    # Search for testlogs under the output_base tree (CI layout).
    local output_base_logs
    output_base_logs=$(find "${REPO_DIR}/.bazel" -type d -name "testlogs" 2>/dev/null | head -n1)
    if [[ -n "$output_base_logs" ]]; then
      log_dir="$output_base_logs"
    fi
  fi

  echo ""
  echo -e "${RED}${BOLD}━━━  ${config_name} FAILURE — Test Log Dump  ━━━${NC}"
  
  local found_logs=0
  local find_args=("$log_dir" -name "test.log")
  # Only apply -newer filter if the timestamp file exists.
  if [[ -f /tmp/dcodex-test-ts-"$config_name" ]]; then
    find_args+=(-newer /tmp/dcodex-test-ts-"$config_name")
  fi
  find_args+=(-print0)

  while IFS= read -r -d '' logfile; do
    found_logs=1
    local rel_path="${logfile#"${log_dir}/"}"
    echo -e "\n${YELLOW}── ${rel_path} ──${NC}"
    # Print last 200 lines to avoid flooding the terminal with 10k-line logs.
    tail -n 200 "$logfile"
    echo -e "${YELLOW}── end ${rel_path} ──${NC}"
  done < <(find "${find_args[@]}" 2>/dev/null)
  
  if [[ $found_logs -eq 0 ]]; then
    echo -e "${YELLOW}  (no test.log files found in ${log_dir})${NC}"
    echo -e "${YELLOW}  Check the build output above for compiler errors.${NC}"
  fi
  echo ""
}

# ── Helper: run one sanitizer suite ───────────────────────────────────────
# Runs a specific set of targets under a given sanitizer config, captures
# the exit status, and dumps logs on failure.
run_sanitizer_suite() {
  local config_name="$1"
  shift
  local targets=("$@")
  
  info "Running ${config_name} tests: ${targets[*]}"
  
  # Purge sandbox dirs from previous suite so no stale state leaks across
  # sanitizer configurations (asan → tsan → msan).
  purge_sandbox_dirs
  
  # Timestamp file for dump_test_logs() to find only fresh logs.
  touch /tmp/dcodex-test-ts-"$config_name"
  
  set +e
  bazel "${BAZEL_JVM_FLAGS[@]}" test \
    --jobs="${BAZEL_JOBS}" \
    --config="$config_name" \
    "${BAZEL_TEST_COMMON[@]}" \
    "${targets[@]}" \
    2>&1 | tee /tmp/dcodex-test-"$config_name".log
  local status=$?
  set -e
  
  if [[ $status -ne 0 ]]; then
    dump_test_logs "$config_name"
  else
    ok "${config_name} tests passed"
  fi
  
  return $status
}

if [[ "$MODE" == "test" ]]; then
  info "Running sanitizer test suites..."
  TEST_START=$(date +%s)
  
  TEST_STATUS_ASAN=0
  TEST_STATUS_MSAN=0
  TEST_STATUS_TSAN=0

  # ── ASan + UBSan ──────────────────────────────────────────────────────
  step "6a/7  ASan + UBSan Tests"
  run_sanitizer_suite asan "${ENGINE_TESTS[@]}" || TEST_STATUS_ASAN=$?

  # ── MSan ──────────────────────────────────────────────────────────────
  # MSan is gated behind RUN_MSAN=1 because it requires ALL linked libraries
  # (including libc++) to be compiled with -fsanitize=memory. The system
  # libc++ installed via apt is NOT instrumented, producing false positives
  # in googletest and abseil internals. To run MSan properly, build a custom
  # LLVM toolchain with an MSan-instrumented libc++ (see:
  # https://clang.llvm.org/docs/MemorySanitizer.html#handling-external-code).
  RUN_MSAN="${RUN_MSAN:-0}"
  MSAN_SKIPPED=0
  step "6b/7  MSan Tests"
  if [[ "$RUN_MSAN" == "1" ]]; then
    # MSan targets exclude sandbox_test (spawns uninstrumented clang++/python3
    # subprocesses which produce false positives) and tsan_checker (TSan-specific).
    MSAN_TARGETS=(
      "//src/engine:warm_worker_pool_test"
      "//src/engine:dynamic_worker_coordinator_test"
    )
    run_sanitizer_suite msan "${MSAN_TARGETS[@]}" || TEST_STATUS_MSAN=$?
  else
    MSAN_SKIPPED=1
    warn "MSan tests SKIPPED (set RUN_MSAN=1 to enable — requires instrumented libc++)"
  fi

  # ── TSan ──────────────────────────────────────────────────────────────
  step "6c/7  TSan Tests"
  
  # TSan: run non-sandbox tests first (matching CI tag filter).
  TSAN_TARGETS=(
    "//src/engine:warm_worker_pool_test"
    "//src/engine:dynamic_worker_coordinator_test"
    "//src/engine:tsan_checker"
  )
  run_sanitizer_suite tsan "${TSAN_TARGETS[@]}" || TEST_STATUS_TSAN=$?
  
  # TSan: sandbox_test runs separately with constrained resources.
  # The sandbox_test forks clang++ which has high memory overhead under TSan.
  if [[ $TEST_STATUS_TSAN -eq 0 ]]; then
    info "Running sandbox_test under TSan (constrained: 1 job, 1 run)..."
    purge_sandbox_dirs
    touch /tmp/dcodex-test-ts-tsan-sandbox
    set +e
    bazel "${BAZEL_JVM_FLAGS[@]}" test \
      --config=tsan \
      "${BAZEL_TEST_COMMON[@]}" \
      --jobs=1 \
      --runs_per_test=1 \
      --local_test_jobs=1 \
      //src/engine:sandbox_test \
      2>&1 | tee -a /tmp/dcodex-test-tsan.log
    TSAN_SANDBOX_STATUS=$?
    set -e
    
    if [[ $TSAN_SANDBOX_STATUS -ne 0 ]]; then
      dump_test_logs "tsan-sandbox"
      TEST_STATUS_TSAN=$TSAN_SANDBOX_STATUS
    else
      ok "TSan sandbox_test passed"
    fi
  else
    warn "Skipping TSan sandbox_test — earlier TSan tests failed"
  fi

  TEST_END=$(date +%s)
  
  # ── Summary ──────────────────────────────────────────────────────────
  echo ""
  echo -e "${BOLD}${CYAN}━━━  Test Summary  ━━━${NC}"
  echo -e "  ASan + UBSan:  $(if [[ $TEST_STATUS_ASAN -eq 0 ]]; then echo -e "${GREEN}PASS${NC}"; else echo -e "${RED}FAIL (exit $TEST_STATUS_ASAN)${NC}"; fi)"
  echo -e "  MSan:          $(if [[ $MSAN_SKIPPED -eq 1 ]]; then echo -e "${YELLOW}SKIP${NC}"; elif [[ $TEST_STATUS_MSAN -eq 0 ]]; then echo -e "${GREEN}PASS${NC}"; else echo -e "${RED}FAIL (exit $TEST_STATUS_MSAN)${NC}"; fi)"
  echo -e "  TSan:          $(if [[ $TEST_STATUS_TSAN -eq 0 ]]; then echo -e "${GREEN}PASS${NC}"; else echo -e "${RED}FAIL (exit $TEST_STATUS_TSAN)${NC}"; fi)"
  echo -e "  Duration:      $(( TEST_END - TEST_START ))s"
  echo -e "  Logs:          /tmp/dcodex-test-{asan,msan,tsan}.log"
  echo ""

  if [[ $TEST_STATUS_ASAN -eq 0 && ($TEST_STATUS_MSAN -eq 0 || $MSAN_SKIPPED -eq 1) && $TEST_STATUS_TSAN -eq 0 ]]; then
    ok "All active test suites passed in $(( TEST_END - TEST_START ))s"
  else
    FAILED_SUITES=""
    [[ $TEST_STATUS_ASAN -ne 0 ]] && FAILED_SUITES+="asan "
    [[ $TEST_STATUS_MSAN -ne 0 && $MSAN_SKIPPED -eq 0 ]] && FAILED_SUITES+="msan "
    [[ $TEST_STATUS_TSAN -ne 0 ]] && FAILED_SUITES+="tsan "
    die "Tests FAILED: ${FAILED_SUITES}— see diagnostic output above and /tmp/dcodex-test-*.log"
  fi
else
  info "Skipping tests (pass --test to run them)"
fi

# ─────────────────────────────────────────────────────────────────────────────
# STEP 7 — Optional: slim image cleanup (Docker layer optimisation)
# ─────────────────────────────────────────────────────────────────────────────
step "7/7  Cleanup"

if [[ "$SLIM_IMAGE" == "1" ]]; then
  info "SLIM_IMAGE=1 — purging build-time tools..."
  DEBIAN_FRONTEND=noninteractive apt-get purge -y gnupg curl wget 2>/dev/null || true
  DEBIAN_FRONTEND=noninteractive apt-get autoremove -y 2>/dev/null || true
  rm -rf /var/lib/apt/lists/*
  ok "Build tools purged (runtime deps retained)"
  warn "Note: disk cache at ${BAZEL_DISK_CACHE} NOT removed — mount as Docker volume to persist"
else
  ok "Cleanup skipped (set SLIM_IMAGE=1 to purge build-time apt packages)"
fi

# Print final cache sizes
info "Disk cache size: $(du -sh "${BAZEL_DISK_CACHE}" 2>/dev/null | cut -f1 || echo "?")"
info "Repo cache size: $(du -sh "${BAZEL_REPO_CACHE}" 2>/dev/null | cut -f1 || echo "?")"

timer

# ─────────────────────────────────────────────────────────────────────────────
# Launch server  (unless --test or --build-only)
# ─────────────────────────────────────────────────────────────────────────────
if [[ "$MODE" == "server" ]]; then
  echo ""
  echo -e "${BOLD}${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
  echo -e "${BOLD}${GREEN}  Starting DCodeX server on :${SERVER_PORT}${NC}"
  echo -e "${BOLD}${GREEN}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
  info "CPU limit:    ${SERVER_CPU_LIMIT}s"
  info "Wall timeout: ${SERVER_WALL_TIMEOUT}s"
  info "Memory limit: $(( SERVER_MEM_LIMIT / 1024 / 1024 ))MB"
  info "Output limit: $(( SERVER_OUTPUT_LIMIT / 1024 ))KB"
  echo ""

  # exec replaces this shell so signals (SIGTERM, SIGINT) go directly to the server.
  exec bazel "${BAZEL_JVM_FLAGS[@]}" run \
    //src/api:server \
    -- \
    --port="${SERVER_PORT}" \
    --sandbox_cpu_time_limit_seconds="${SERVER_CPU_LIMIT}" \
    --sandbox_wall_clock_timeout_seconds="${SERVER_WALL_TIMEOUT}" \
    --sandbox_memory_limit_bytes="${SERVER_MEM_LIMIT}" \
    --sandbox_max_output_bytes="${SERVER_OUTPUT_LIMIT}"
fi

echo -e "\n${GREEN}${BOLD}Done.${NC} Total time: $(( $(date +%s) - SCRIPT_START ))s"
