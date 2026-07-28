# shellcheck shell=bash
#
# ESP-IDF environment activation for the build system.
#
# MUST be sourced, never executed: it exports IDF_PATH (and the rest of the IDF
# environment) into the calling shell. The Makefile wires it up as
#     EIM_ACTIVATE := . scripts/idf_env.sh
# so every recipe keeps the familiar `$(EIM_ACTIVATE) && <command>` form.
#
# What it guarantees, in order:
#   1. When the EIM activate script for $EIM_IDF_VERSION is installed, source it.
#   2. If that activation replaced an IDF_PATH the caller had already exported
#      (export.sh, Docker image, CI), say so loudly. The EIM script overrides
#      IDF_PATH unconditionally and used to do it silently, which once had us
#      chasing a bug for days in a build that ran against a different IDF than
#      we thought.
#   3. IDF_PATH must end up set and pointing at something that looks like an IDF
#      checkout, whichever way it got there: the activate script may be absent
#      (Docker espressif/idf image, Jenkins with export.sh) and IDF_PATH must
#      then already be in the environment, and a half-removed EIM install may
#      run its activate script without setting IDF_PATH at all. Either way, fail
#      instead of silently building against whatever happens to be on PATH.
#   4. Verify that the IDF actually in the environment is $EIM_IDF_VERSION, by
#      reading esp_idf_version.h (works in Docker and without a git checkout).
#      A mismatch is a hard error.
#
# All diagnostics go to stderr: several recipes capture stdout (gcovr, python -c,
# make -s qemu-bin-path), so a single stray stdout line would corrupt them.
#
# Escape hatch: IDF_VERSION_CHECK=0 skips step 4 only — steps 1-3 still run, so
# IDF_PATH must be set and look like an IDF checkout even with the check off.
# IDF_ENV_QUIET=1 suppresses the "Using ESP-IDF ..." banner (errors still print).

# Refuse to run as a program — `return` below only works in a sourced script,
# and an executed copy would export nothing into the caller anyway.
if [ "${BASH_SOURCE[0]:-}" = "${0:-}" ]; then
    echo "ERROR: scripts/idf_env.sh must be sourced, not executed." >&2
    exit 1
fi

# Expected IDF tag, e.g. "v5.4.4". Exported by the Makefile.
_IDF_ENV_TAG="${EIM_IDF_VERSION:-}"
if [ -z "$_IDF_ENV_TAG" ]; then
    echo "ERROR: EIM_IDF_VERSION is not set. Source this script via the Makefile" >&2
    echo "       (EIM_ACTIVATE), or set EIM_IDF_VERSION yourself, e.g. v5.4.4." >&2
    unset _IDF_ENV_TAG
    return 1
fi

# IDF_PATH exactly as the caller had it, so we can detect a silent override.
_IDF_ENV_CALLER_IDF_PATH="${IDF_PATH:-}"
_IDF_ENV_ACTIVATE_SCRIPT="${HOME:-}/.espressif/tools/activate_idf_${_IDF_ENV_TAG}.sh"

if [ -f "$_IDF_ENV_ACTIVATE_SCRIPT" ]; then
    # The EIM script is not written to be sourced into a strict shell:
    #   * it reads variables that may be unset (ZSH_VERSION & co), which aborts
    #     the caller's shell under `set -u`;
    #   * it ends with an `eim select <tag>` whose status leaks out as the status
    #     of the whole script, so under `set -e` a failing (or simply absent)
    #     eim kills the caller mid-source, before a single diagnostic is printed.
    # Relax nounset, errexit and pipefail across the source and put the caller's
    # settings back afterwards. The restore is two-way — each flag is explicitly
    # turned back off when the caller did not have it — because the sourced
    # script is free to set its own `set -euo pipefail`, and leaving that on
    # would leak strict-shell semantics into the rest of the make recipe.
    # Restoring uses `case`/`if` on purpose: a bare `[ ... ] && set -o pipefail`
    # would itself return non-zero when pipefail was off and abort the caller
    # under the errexit we have just restored.
    _IDF_ENV_SAVED_FLAGS="$-"
    # Empty on shells without pipefail, "on"/"off" on the shells that have it.
    _IDF_ENV_SAVED_PIPEFAIL=$(set -o 2>/dev/null | awk '$1 == "pipefail" { print $2 }')
    set +u
    set +e
    if [ -n "$_IDF_ENV_SAVED_PIPEFAIL" ]; then
        set +o pipefail
    fi
    # stdout holds the verbose EIM activation banner — drop it, keep stderr.
    # The explicit empty argument shields the EIM script from the positional
    # parameters a sourced script inherits from its caller: it starts with
    # `if [ "$1" = "-e" ]; then print_env_variables; exit 0`, so being sourced
    # from a shell whose $1 happens to be "-e" would exit the caller's shell with
    # status 0, having activated nothing — exactly the silent success this
    # script exists to prevent.
    . "$_IDF_ENV_ACTIVATE_SCRIPT" "" >/dev/null
    case "$_IDF_ENV_SAVED_FLAGS" in
        *u*) set -u ;;
        *)   set +u ;;
    esac
    if [ "$_IDF_ENV_SAVED_PIPEFAIL" = "on" ]; then
        set -o pipefail
    elif [ -n "$_IDF_ENV_SAVED_PIPEFAIL" ]; then
        set +o pipefail
    fi
    # errexit goes back last: every restore above must still run without it.
    case "$_IDF_ENV_SAVED_FLAGS" in
        *e*) set -e ;;
        *)   set +e ;;
    esac
    unset _IDF_ENV_SAVED_FLAGS _IDF_ENV_SAVED_PIPEFAIL
    if [ -z "${IDF_PATH:-}" ]; then
        echo "ERROR: the EIM activate script ran but left IDF_PATH unset." >&2
        echo "       script: ${_IDF_ENV_ACTIVATE_SCRIPT}" >&2
        echo "       The EIM installation of ${_IDF_ENV_TAG} is probably damaged —" >&2
        echo "       the activate script is still there but exports no IDF_PATH." >&2
        echo "       Reinstall it (\`eim install --idf-version ${_IDF_ENV_TAG}\`)," >&2
        echo "       or source the IDF export.sh before running make." >&2
        unset _IDF_ENV_TAG _IDF_ENV_CALLER_IDF_PATH _IDF_ENV_ACTIVATE_SCRIPT
        return 1
    fi
    if [ -n "$_IDF_ENV_CALLER_IDF_PATH" ] && [ "${IDF_PATH:-}" != "$_IDF_ENV_CALLER_IDF_PATH" ]; then
        echo "WARNING: EIM activation replaced the IDF_PATH set by the caller." >&2
        echo "WARNING:   caller had : ${_IDF_ENV_CALLER_IDF_PATH}" >&2
        echo "WARNING:   now using  : ${IDF_PATH:-<unset>}" >&2
        echo "WARNING: The build will use the EIM copy. Uninstall the stray IDF or" >&2
        echo "WARNING: set EIM_IDF_VERSION to the version you actually want." >&2
    fi
elif [ -z "${IDF_PATH:-}" ]; then
    echo "ERROR: ESP-IDF ${_IDF_ENV_TAG} is not available." >&2
    echo "       No EIM activate script at ${_IDF_ENV_ACTIVATE_SCRIPT}" >&2
    echo "       and IDF_PATH is not set in the environment." >&2
    echo "       Install it with EIM (\`eim install --idf-version ${_IDF_ENV_TAG}\`)," >&2
    echo "       or source the IDF export.sh before running make." >&2
    unset _IDF_ENV_TAG _IDF_ENV_CALLER_IDF_PATH _IDF_ENV_ACTIVATE_SCRIPT
    return 1
fi

# Every branch above either set IDF_PATH or returned. That it points at a real
# IDF checkout is verified unconditionally, before anything is allowed to use it:
# IDF_VERSION_CHECK=0 waives the version COMPARISON of step 4, not the existence
# of the IDF the build runs against. Skipping this check used to let the script
# return success with an empty IDF_PATH, after which every recipe happily ran
# "$IDF_PATH/tools/idf.py" = "/tools/idf.py".
_IDF_ENV_VERSION_HEADER="${IDF_PATH}/components/esp_common/include/esp_idf_version.h"
if [ ! -d "$IDF_PATH" ] || [ ! -f "$_IDF_ENV_VERSION_HEADER" ]; then
    echo "ERROR: IDF_PATH does not point at an ESP-IDF checkout." >&2
    echo "       IDF_PATH : ${IDF_PATH:-<empty>}" >&2
    echo "       missing  : ${_IDF_ENV_VERSION_HEADER}" >&2
    echo "       Install ESP-IDF ${_IDF_ENV_TAG} with EIM (\`eim install" >&2
    echo "       --idf-version ${_IDF_ENV_TAG}\`), or point IDF_PATH at a real" >&2
    echo "       checkout (source its export.sh) before running make." >&2
    echo "       IDF_VERSION_CHECK=0 does NOT bypass this check." >&2
    unset _IDF_ENV_TAG _IDF_ENV_CALLER_IDF_PATH _IDF_ENV_ACTIVATE_SCRIPT \
          _IDF_ENV_VERSION_HEADER
    return 1
fi

# Read the installed version from the header rather than from git: the Docker
# image ships IDF without usable git metadata, and the header is authoritative.
_IDF_ENV_FOUND=$(awk '
    $1 == "#define" && $2 == "ESP_IDF_VERSION_MAJOR" { major = $3 }
    $1 == "#define" && $2 == "ESP_IDF_VERSION_MINOR" { minor = $3 }
    $1 == "#define" && $2 == "ESP_IDF_VERSION_PATCH" { patch = $3 }
    END {
        if (major != "" && minor != "" && patch != "") {
            print major "." minor "." patch
        }
    }
' "$_IDF_ENV_VERSION_HEADER")

# Normalise the expected tag to MAJOR.MINOR.PATCH: EIM names v5.4 what the
# header reports as 5.4.0.
_IDF_ENV_EXPECTED="${_IDF_ENV_TAG#v}"
case "$_IDF_ENV_EXPECTED" in
    *.*.*) ;;
    *.*)   _IDF_ENV_EXPECTED="${_IDF_ENV_EXPECTED}.0" ;;
    *)     _IDF_ENV_EXPECTED="${_IDF_ENV_EXPECTED}.0.0" ;;
esac

if [ "${IDF_VERSION_CHECK:-1}" != "0" ]; then
    if [ -z "$_IDF_ENV_FOUND" ]; then
        echo "ERROR: cannot determine the ESP-IDF version in use." >&2
        echo "       Expected ${_IDF_ENV_EXPECTED} (EIM_IDF_VERSION=${_IDF_ENV_TAG})." >&2
        echo "       Could not read ESP_IDF_VERSION_* from ${_IDF_ENV_VERSION_HEADER}" >&2
        echo "       Is IDF_PATH=${IDF_PATH} really an ESP-IDF checkout?" >&2
        echo "       Set IDF_VERSION_CHECK=0 to bypass this check." >&2
        unset _IDF_ENV_TAG _IDF_ENV_CALLER_IDF_PATH _IDF_ENV_ACTIVATE_SCRIPT \
              _IDF_ENV_VERSION_HEADER _IDF_ENV_FOUND _IDF_ENV_EXPECTED
        return 1
    fi
    if [ "$_IDF_ENV_FOUND" != "$_IDF_ENV_EXPECTED" ]; then
        # Suggest the tag EIM itself uses: it names the .0 releases without the
        # patch component (activate_idf_v5.4.sh, not activate_idf_v5.4.0.sh), so
        # advising "v5.4.0" would send the user straight back into this error.
        _IDF_ENV_SUGGEST="${_IDF_ENV_FOUND%.0}"
        echo "ERROR: wrong ESP-IDF version." >&2
        echo "       expected : ${_IDF_ENV_EXPECTED} (EIM_IDF_VERSION=${_IDF_ENV_TAG})" >&2
        echo "       found    : ${_IDF_ENV_FOUND}" >&2
        echo "       at       : ${IDF_PATH}" >&2
        echo "       Install the expected version with EIM, or override the" >&2
        echo "       expectation (make EIM_IDF_VERSION=v${_IDF_ENV_SUGGEST} ...)." >&2
        echo "       Set IDF_VERSION_CHECK=0 to bypass this check." >&2
        unset _IDF_ENV_TAG _IDF_ENV_CALLER_IDF_PATH _IDF_ENV_ACTIVATE_SCRIPT \
              _IDF_ENV_VERSION_HEADER _IDF_ENV_FOUND _IDF_ENV_EXPECTED \
              _IDF_ENV_SUGGEST
        return 1
    fi
fi

# The banner is the main guard against "we built with the wrong IDF", so it stays
# on by default. IDF_ENV_QUIET=1 silences it for recipes that source this script
# in a loop (make unittests), where 35 identical lines only bury the real output.
if [ "${IDF_ENV_QUIET:-0}" != "1" ]; then
    if [ -n "$_IDF_ENV_FOUND" ]; then
        echo "Using ESP-IDF v${_IDF_ENV_FOUND} from ${IDF_PATH}" >&2
    else
        # Only reachable with IDF_VERSION_CHECK=0: the header is in place (checked
        # above) but its ESP_IDF_VERSION_* macros could not be read. Say that,
        # instead of printing an invented "vunknown" that reads like a version.
        echo "Using ESP-IDF of an unreadable version from ${IDF_PATH}" >&2
        echo "  (IDF_VERSION_CHECK=0, and ${_IDF_ENV_VERSION_HEADER} did not parse)" >&2
    fi
fi

unset _IDF_ENV_TAG _IDF_ENV_CALLER_IDF_PATH _IDF_ENV_ACTIVATE_SCRIPT \
      _IDF_ENV_VERSION_HEADER _IDF_ENV_FOUND _IDF_ENV_EXPECTED
return 0
