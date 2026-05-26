#!/usr/bin/env bash
# Drive tests/remote_to_remote_probe.py across every combination of
#   (etc client binary)  X  (source etd binary)  X  (destination etd binary)
# using the per-branch builds the user keeps next to the repo.
#
# Each tuple counts as a single test. The exit code of the underlying
# `etc` invocation determines pass/fail; full per-combination logs land
# in --logdir for inspection of any failures.
#
# Result is summarised as one "src vs dst" grid per etc version so that
# the cross-version interaction pattern is immediately visible.

set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
PROBE="$SCRIPT_DIR/remote_to_remote_probe.py"

# Default set of versions. Each one is expected to have a sibling build
# directory of the form ${REPO_ROOT}/<uname-s>-<uname-m>-native-opt-<version>
# containing the ready-built `etc` and `etd` binaries.
ALL_VERSIONS=(1.0 1.1 master issue-30)

# Mirrors what the Makefile computes for its build directory:
#   $(shell uname -sm | sed 's/\( \{1,\}\)/-/g')-$(B2B)-$(BUILD)
# i.e. "Darwin-arm64-native-opt" on this machine.
ARCH_PREFIX="$(uname -sm | sed 's/[[:space:]]\{1,\}/-/g')-native-opt"

usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

Run remote_to_remote_probe.py over every (etc, src-etd, dst-etd) tuple
drawn from the requested version sets.

Options:
  --etc-versions "v1 v2 ..."   etc client versions to test (default: all)
  --src-versions "v1 v2 ..."   source daemon versions (default: all)
  --dst-versions "v1 v2 ..."   destination daemon versions (default: all)
  --timeout SECS               per-tuple timeout, passed to the probe (default: 30)
  --logdir DIR                 where per-tuple logs are stored
                               (default: \$REPO_ROOT/tests/version_matrix_logs)
  --keep-logs                  do not wipe the logdir before running
  -h, --help                   this help

Known versions: ${ALL_VERSIONS[*]}
Each version maps to:
  ${REPO_ROOT}/${ARCH_PREFIX}-<version>/{etc,etd}
EOF
}

etc_versions=("${ALL_VERSIONS[@]}")
src_versions=("${ALL_VERSIONS[@]}")
dst_versions=("${ALL_VERSIONS[@]}")
timeout_s=30
logdir="${REPO_ROOT}/tests/version_matrix_logs"
keep_logs=0

while [[ $# -gt 0 ]]; do
    case "$1" in
        --etc-versions) read -r -a etc_versions <<<"$2"; shift 2 ;;
        --src-versions) read -r -a src_versions <<<"$2"; shift 2 ;;
        --dst-versions) read -r -a dst_versions <<<"$2"; shift 2 ;;
        --timeout)      timeout_s="$2"; shift 2 ;;
        --logdir)       logdir="$2"; shift 2 ;;
        --keep-logs)    keep_logs=1; shift ;;
        -h|--help)      usage; exit 0 ;;
        *) echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

bin_for() {
    # $1 = "etc" or "etd"; $2 = version label
    echo "${REPO_ROOT}/${ARCH_PREFIX}-$2/$1"
}

# Pre-flight: every binary referenced by the requested matrix must exist
# and be executable.
missing=0
for v in "${etc_versions[@]}"; do
    bin="$(bin_for etc "$v")"
    if [[ ! -x "$bin" ]]; then
        echo "missing etc binary for version '$v' (looked at: $bin)" >&2
        missing=1
    fi
done
for v in "${src_versions[@]}" "${dst_versions[@]}"; do
    bin="$(bin_for etd "$v")"
    if [[ ! -x "$bin" ]]; then
        echo "missing etd binary for version '$v' (looked at: $bin)" >&2
        missing=1
    fi
done
if (( missing != 0 )); then
    exit 2
fi

if (( keep_logs == 0 )); then
    rm -rf "$logdir"
fi
mkdir -p "$logdir"

# Storage of per-tuple results. macOS still ships bash 3.2 which has no
# associative arrays, so we keep records in a tempfile and look them up
# with awk for the summary. Format: tab-separated
#   etc_v <TAB> src_v <TAB> dst_v <TAB> verdict
results_file="$(mktemp "${TMPDIR:-/tmp}/run_version_matrix.XXXXXX")"
trap 'rm -f "$results_file"' EXIT

record() {
    # $1=etc $2=src $3=dst $4=verdict
    printf '%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" >> "$results_file"
}
lookup() {
    # $1=etc $2=src $3=dst -> prints verdict or empty string
    awk -F'\t' -v e="$1" -v s="$2" -v d="$3" \
        '$1==e && $2==s && $3==d {print $4; exit}' "$results_file"
}

total=0
passed=0
start_all=$(date +%s)

for etc_v in "${etc_versions[@]}"; do
    for src_v in "${src_versions[@]}"; do
        for dst_v in "${dst_versions[@]}"; do
            total=$((total + 1))
            label="etc-${etc_v}__src-${src_v}__dst-${dst_v}"
            log="${logdir}/${label}.log"
            printf "  running %-44s ... " "$label"
            start=$(date +%s)
            if python3 "$PROBE" \
                    --client="$(bin_for etc "$etc_v")" \
                    --source-daemon="$(bin_for etd "$src_v")" \
                    --dest-daemon="$(bin_for etd "$dst_v")" \
                    --timeout="$timeout_s" \
                    >"$log" 2>&1; then
                rc=0
            else
                rc=$?
            fi
            dt=$(( $(date +%s) - start ))
            if (( rc == 0 )); then
                record "$etc_v" "$src_v" "$dst_v" "PASS"
                passed=$((passed + 1))
                printf "PASS (%ds)\n" "$dt"
            elif (( rc == 124 )); then
                record "$etc_v" "$src_v" "$dst_v" "TIMEOUT"
                printf "TIMEOUT (%ds)\n" "$dt"
            else
                record "$etc_v" "$src_v" "$dst_v" "FAIL(${rc})"
                printf "FAIL rc=%d (%ds)\n" "$rc" "$dt"
            fi
        done
    done
done

end_all=$(date +%s)

# Mirror the human-readable summary to a file alongside the per-tuple logs
# so it can be archived / diffed after the run.
summary_file="${logdir}/summary.txt"
{
    echo "================ summary ================"
    echo "date:    $(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "logs in: $logdir"
    echo "$passed / $total passed in $((end_all - start_all))s"

    cell_width=14
    for etc_v in "${etc_versions[@]}"; do
        echo
        echo "etc = $etc_v"
        printf "  %-12s" 'src \ dst'
        for dst_v in "${dst_versions[@]}"; do
            printf " %-${cell_width}s" "$dst_v"
        done
        printf "\n"
        for src_v in "${src_versions[@]}"; do
            printf "  %-12s" "$src_v"
            for dst_v in "${dst_versions[@]}"; do
                printf " %-${cell_width}s" "$(lookup "$etc_v" "$src_v" "$dst_v")"
            done
            printf "\n"
        done
    done
} | tee "$summary_file"

echo
echo "summary written to: $summary_file"

# Non-zero exit if any tuple failed, so CI / wrappers can pick it up.
if (( passed == total )); then
    exit 0
else
    exit 1
fi
