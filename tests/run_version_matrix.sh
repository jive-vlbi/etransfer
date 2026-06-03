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

# Versions whose etd binary can host an SRT data-channel listener. Used by
# the srt sub-pass to decide whether a given daemon should be configured
# with "srt,udt" or just "udt": pre-issue-30 etds do not recognise srt://
# URLs and would refuse to start, so we only hand them schemes they know.
SRT_CAPABLE_VERSIONS=(issue-30)

# Mirrors what the Makefile computes for its build directory:
#   $(shell uname -sm | sed 's/\( \{1,\}\)/-/g')-$(B2B)-$(BUILD)
# i.e. "Darwin-arm64-native-opt" on this machine.
ARCH_PREFIX="$(uname -sm | sed 's/[[:space:]]\{1,\}/-/g')-native-opt"

usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

Run remote_to_remote_probe.py over every (etc, src-etd, dst-etd) tuple
drawn from the requested version sets. The matrix is executed in two
sub-passes:

  pass "tcp"  baseline    every daemon listens on tcp only
  pass "srt"  SRT exposure SRT-capable daemons listen on "srt,udt" with SRT
                          listed first (so a source daemon would, without
                          the version-aware filter in ETDProxy::sendFile,
                          prefer SRT and hang against a pre-v3 source);
                          other daemons listen on udt. Tuples whose dst is
                          not SRT-capable degrade to a UDT regression test,
                          which complements the tcp baseline.

Options:
  --etc-versions "v1 v2 ..."   etc client versions to test (default: all)
  --src-versions "v1 v2 ..."   source daemon versions (default: all)
  --dst-versions "v1 v2 ..."   destination daemon versions (default: all)
  --timeout SECS               per-tuple timeout, passed to the probe (default: 30)
  --logdir DIR                 where per-tuple logs are stored. One sub-
                               directory per pass is created inside it.
                               (default: \$REPO_ROOT/tests/version_matrix_logs)
  --keep-logs                  do not wipe the logdir before running
  -h, --help                   this help

Known versions: ${ALL_VERSIONS[*]}
Each version maps to:
  ${REPO_ROOT}/${ARCH_PREFIX}-<version>/{etc,etd}

SRT-capable versions: ${SRT_CAPABLE_VERSIONS[*]}
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
#   pass <TAB> etc_v <TAB> src_v <TAB> dst_v <TAB> verdict
results_file="$(mktemp "${TMPDIR:-/tmp}/run_version_matrix.XXXXXX")"
trap 'rm -f "$results_file"' EXIT

record() {
    # $1=pass $2=etc $3=src $4=dst $5=verdict
    printf '%s\t%s\t%s\t%s\t%s\n' "$1" "$2" "$3" "$4" "$5" >> "$results_file"
}
lookup() {
    # $1=pass $2=etc $3=src $4=dst -> prints verdict or empty string
    awk -F'\t' -v p="$1" -v e="$2" -v s="$3" -v d="$4" \
        '$1==p && $2==e && $3==s && $4==d {print $5; exit}' "$results_file"
}

# Returns 0 if $1 (version) is in SRT_CAPABLE_VERSIONS, non-zero otherwise.
version_is_srt_capable() {
    local v="$1" cap
    for cap in "${SRT_CAPABLE_VERSIONS[@]}"; do
        [[ "$v" == "$cap" ]] && return 0
    done
    return 1
}

# Per-pass scheme selection. Each pass has a fixed policy for converting a
# (role, version) pair into the daemon's --data scheme list.
#
#   tcp pass: every daemon gets "tcp" -- baseline regression coverage.
#   srt pass: SRT-capable daemons get "srt,udt" with SRT listed FIRST so
#             that a source daemon dialled with this list would prefer SRT
#             (and, without the version-aware filter in ETDProxy::sendFile,
#             hang against a pre-v3 source). Other daemons get "udt", their
#             richest universally-understood transport.
schemes_for() {
    # $1=pass $2=version  ->  prints scheme spec
    local pass="$1" version="$2"
    case "$pass" in
        tcp)
            echo "tcp"
            ;;
        srt)
            if version_is_srt_capable "$version"; then
                echo "srt,udt"
            else
                echo "udt"
            fi
            ;;
        *)
            echo "tcp"
            ;;
    esac
}

# Both passes run the full (etc x src x dst) grid. Tuples that do not
# involve any SRT-capable daemon in the srt pass simply degrade to a UDT
# regression test (pass 1 covers TCP), which is a useful side-benefit of
# keeping both grids the same shape so they can be diffed side by side.
declare -a PASSES=(tcp srt)

run_pass() {
    # $1=pass name
    local pass="$1"
    local pass_logdir="${logdir}/${pass}"
    mkdir -p "$pass_logdir"

    echo
    echo "================ pass '${pass}' ================"

    for etc_v in "${etc_versions[@]}"; do
        for src_v in "${src_versions[@]}"; do
            for dst_v in "${dst_versions[@]}"; do
                total=$((total + 1))
                tuple_label="etc-${etc_v}__src-${src_v}__dst-${dst_v}"
                log="${pass_logdir}/${tuple_label}.log"
                printf "  [%s] %-44s ... " "$pass" "$tuple_label"

                local src_schemes dst_schemes
                src_schemes="$(schemes_for "$pass" "$src_v")"
                dst_schemes="$(schemes_for "$pass" "$dst_v")"

                start=$(date +%s)
                if python3 "$PROBE" \
                        --client="$(bin_for etc "$etc_v")" \
                        --source-daemon="$(bin_for etd "$src_v")" \
                        --dest-daemon="$(bin_for etd "$dst_v")" \
                        --source-data-scheme="$src_schemes" \
                        --dest-data-scheme="$dst_schemes" \
                        --timeout="$timeout_s" \
                        >"$log" 2>&1; then
                    rc=0
                else
                    rc=$?
                fi
                dt=$(( $(date +%s) - start ))
                if (( rc == 0 )); then
                    record "$pass" "$etc_v" "$src_v" "$dst_v" "PASS"
                    passed=$((passed + 1))
                    printf "PASS (%ds)\n" "$dt"
                elif (( rc == 124 )); then
                    record "$pass" "$etc_v" "$src_v" "$dst_v" "TIMEOUT"
                    printf "TIMEOUT (%ds)\n" "$dt"
                else
                    record "$pass" "$etc_v" "$src_v" "$dst_v" "FAIL(${rc})"
                    printf "FAIL rc=%d (%ds)\n" "$rc" "$dt"
                fi
            done
        done
    done
}

total=0
passed=0
start_all=$(date +%s)

for pass in "${PASSES[@]}"; do
    run_pass "$pass"
done

end_all=$(date +%s)

# Mirror the human-readable summary to a file alongside the per-tuple logs
# so it can be archived / diffed after the run.
summary_file="${logdir}/summary.txt"
{
    echo "================ summary ================"
    echo "date:    $(date '+%Y-%m-%d %H:%M:%S %z')"
    echo "passes:  ${PASSES[*]}"
    echo "logs in: $logdir"
    echo "$passed / $total passed in $((end_all - start_all))s"

    cell_width=14
    for pass in "${PASSES[@]}"; do
        echo
        echo "---- pass '${pass}' ----"
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
                    printf " %-${cell_width}s" "$(lookup "$pass" "$etc_v" "$src_v" "$dst_v")"
                done
                printf "\n"
            done
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
