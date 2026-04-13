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

# Server flags
SERVER_PORT="${SERVER_PORT:-50051}"
SERVER_CPU_LIMIT="${SERVER_CPU_LIMIT:-1}"
SERVER_WALL_TIMEOUT="${SERVER_WALL_TIMEOUT:-3}"
SERVER_MEM_LIMIT="${SERVER_MEM_LIMIT:-52428800}"  # 50 MB
SERVER_OUTPUT_LIMIT="${SERVER_OUTPUT_LIMIT:-1024}" # 1 KB

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
    wget curl gnupg ca-certificates git \
    python3 python3-pip python3-venv python-is-python3 \
    libexpat1 libsqlite3-0 zlib1g \
    binutils-gold \
    2>/dev/null
  ok "Core packages installed"

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
      2>/dev/null
    ok "LLVM ${LLVM_VERSION} installed"
  fi

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

timer

# ─────────────────────────────────────────────────────────────────────────────
# STEP 3 — Python + gRPC tooling
# ─────────────────────────────────────────────────────────────────────────────
step "3/7  Python & gRPC tooling"

info "Upgrading pip..."
python -m pip install --quiet --upgrade pip

info "Installing grpcio-tools and client requirements..."
python -m pip install --quiet \
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

timer

# ─────────────────────────────────────────────────────────────────────────────
# STEP 5 — Build
# ─────────────────────────────────────────────────────────────────────────────
step "5/7  Bazel build  (${BAZEL_JOBS} jobs)"
cd "${REPO_DIR}"

# Override JVM heap for this machine's available memory.
# The .bazelrc has M1-tuned 16g; on a Linux CI runner that may OOM the JVM.
BAZEL_JVM_FLAGS=(
  "--host_jvm_args=-Xmx${BAZEL_MEM_MB}m"
  "--host_jvm_args=-XX:+UseG1GC"
  "--host_jvm_args=-XX:MaxGCPauseMillis=50"
)

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

if [[ "$MODE" == "test" ]]; then
  info "Running sandbox integration tests..."
  TEST_START=$(date +%s)

  bazel "${BAZEL_JVM_FLAGS[@]}" test \
    --jobs="${BAZEL_JOBS}" \
    //src/engine:sandbox_test \
    --test_output=all \
    --test_env=HOME=/tmp \
    2>&1 | tee /tmp/dcodex-test.log

  TEST_STATUS=${PIPESTATUS[0]}
  TEST_END=$(date +%s)

  if [[ $TEST_STATUS -eq 0 ]]; then
    ok "All tests passed in $(( TEST_END - TEST_START ))s"
  else
    die "Tests FAILED (exit ${TEST_STATUS}) — see /tmp/dcodex-test.log"
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
  info "Output limit: ${SERVER_OUTPUT_LIMIT} bytes"
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
