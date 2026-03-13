#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat <<'EOF'
Usage: dev/tools/install-tcl-test-prereqs.sh [--check|--print|--install]

Checks for the Tcllib packages needed to unskip parts of the Tcl regression
suite:

  - json   : needed by tst/json.test
  - sha1   : needed by tst/set-manifest.test and tst/unversioned.test

Modes:
  --check   Report whether the required packages are already available.
  --print   Print a likely install command for the current platform.
  --install Attempt to install Tcllib using an available package manager.

The install step is best-effort and intentionally narrow: it only installs
Tcllib, which provides both the json and sha1 packages used by the tests.
EOF
}

mode="check"
if [[ $# -gt 1 ]]; then
  usage >&2
  exit 64
elif [[ $# -eq 1 ]]; then
  case "$1" in
    --check) mode="check" ;;
    --print) mode="print" ;;
    --install) mode="install" ;;
    -h|--help) usage; exit 0 ;;
    *)
      usage >&2
      exit 64
      ;;
  esac
fi

check_pkg() {
  local pkg="$1"
  tclsh <<EOF >/dev/null 2>&1
if {[catch {package require $pkg}] != 0} {
  exit 1
}
EOF
}

json_ok=0
sha1_ok=0
check_pkg json && json_ok=1
check_pkg sha1 && sha1_ok=1

if [[ "$mode" == "check" ]]; then
  printf 'json=%s\n' "$json_ok"
  printf 'sha1=%s\n' "$sha1_ok"
  if [[ "$json_ok" -eq 1 && "$sha1_ok" -eq 1 ]]; then
    echo "Tcl test prerequisites are installed."
    exit 0
  fi
  echo "Missing Tcl test prerequisites detected."
  echo "Run with --print to see the likely install command."
  exit 1
fi

suggest_install_cmd() {
  if command -v apt-get >/dev/null 2>&1; then
    printf '%s\n' 'sudo apt-get update && sudo apt-get install -y tcllib'
    return 0
  fi
  if command -v dnf >/dev/null 2>&1; then
    printf '%s\n' 'sudo dnf install -y tcllib'
    return 0
  fi
  if command -v yum >/dev/null 2>&1; then
    printf '%s\n' 'sudo yum install -y tcllib'
    return 0
  fi
  if command -v pacman >/dev/null 2>&1; then
    printf '%s\n' 'sudo pacman -S --needed tcllib'
    return 0
  fi
  if command -v brew >/dev/null 2>&1; then
    printf '%s\n' 'brew install tcl-tk'
    return 0
  fi
  if command -v port >/dev/null 2>&1; then
    printf '%s\n' 'sudo port install tcllib'
    return 0
  fi
  if command -v teacup >/dev/null 2>&1; then
    printf '%s\n' 'teacup install json json::write sha1'
    return 0
  fi
  return 1
}

if [[ "$mode" == "print" ]]; then
  if suggest_install_cmd; then
    exit 0
  fi
  echo "No supported package manager detected. Install Tcllib manually." >&2
  exit 1
fi

if [[ "$json_ok" -eq 1 && "$sha1_ok" -eq 1 ]]; then
  echo "Tcl test prerequisites are already installed."
  exit 0
fi

if command -v apt-get >/dev/null 2>&1; then
  sudo apt-get update
  sudo apt-get install -y tcllib
elif command -v dnf >/dev/null 2>&1; then
  sudo dnf install -y tcllib
elif command -v yum >/dev/null 2>&1; then
  sudo yum install -y tcllib
elif command -v pacman >/dev/null 2>&1; then
  sudo pacman -S --needed tcllib
elif command -v brew >/dev/null 2>&1; then
  brew install tcl-tk
elif command -v port >/dev/null 2>&1; then
  sudo port install tcllib
elif command -v teacup >/dev/null 2>&1; then
  teacup install json json::write sha1
else
  echo "No supported package manager detected. Install Tcllib manually." >&2
  exit 1
fi

check_pkg json && check_pkg sha1
echo "Tcl test prerequisites installed."
