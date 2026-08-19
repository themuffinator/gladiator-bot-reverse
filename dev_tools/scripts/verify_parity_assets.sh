#!/usr/bin/env bash
set -euo pipefail

red() { printf "\033[31m%s\033[0m" "$1"; }
green() { printf "\033[32m%s\033[0m" "$1"; }
yellow() { printf "\033[33m%s\033[0m" "$1"; }

project_root="$(cd "$(dirname "$0")/.." && pwd)"
asset_root="${GLADIATOR_ASSET_DIR:-"${project_root}/dev_tools/assets"}"

missing=()
optional_missing=()

require_file() {
	local description="$1"
	local path="$2"
	if [[ ! -f "$path" ]]; then
		missing+=("${description} (${path})")
	fi
}

# Optional assets gate individual test cases that skip themselves when the
# file is absent. They must not fail this script: it is the parity suite's
# setup fixture, so a hard failure here disables every parity executable
# over a fixture only one test case consults.
optional_file() {
	local description="$1"
	local path="$2"
	if [[ ! -f "$path" ]]; then
		optional_missing+=("${description} (${path})")
	fi
}

optional_file "mover BSP" "${asset_root}/maps/test_mover.bsp"
optional_file "mover AAS" "${asset_root}/maps/test_mover.aas"
require_file "precompiler lexer source (fw_items.c)" "${asset_root}/fw_items.c"
require_file "precompiler lexer source (syn.c)" "${asset_root}/syn.c"
require_file "weapon configuration stub (weapons.c)" "${asset_root}/weapons.c"
require_file "default weapon weights (defaul_w.c)" "${asset_root}/default/defaul_w.c"
require_file "default item weights (defaul_i.c)" "${asset_root}/default/defaul_i.c"

if [[ ${#missing[@]} -gt 0 ]]; then
	printf "%s\n" "$(red "Parity asset verification failed:")"
	for entry in "${missing[@]}"; do
		printf " - %s\n" "$entry"
	done
	cat <<EOF_MSG

Stage the listed assets under ${GLADIATOR_ASSET_DIR:-${asset_root}} (or set GLADIATOR_ASSET_DIR to the correct asset root)
before rerunning the parity suite. See tests/README.md for download locations and layout requirements.
EOF_MSG
	exit 1
fi

if [[ ${#optional_missing[@]} -gt 0 ]]; then
	printf "%s\n" "$(yellow "Parity optional assets absent (dependent cases will skip):")"
	for entry in "${optional_missing[@]}"; do
		printf " - %s\n" "$entry"
	done
fi

printf "%s %s\n" "$(green "Parity assets verified:")" "${asset_root}"

if [[ -n "${GLADIATOR_Q2_BASEDIR:-}" || -n "${GLADIATOR_Q2_DEDICATED_SERVER:-}" ]]; then
	if [[ -z "${GLADIATOR_Q2_BASEDIR:-}" ]]; then
		printf "%s\n" "$(yellow "GLADIATOR_Q2_BASEDIR is unset; headless Quake II parity will skip.")"
	else
		require_q2=()
		[[ ! -d "${GLADIATOR_Q2_BASEDIR}/baseq2" ]] && require_q2+=("baseq2 asset directory (${GLADIATOR_Q2_BASEDIR}/baseq2)")
		[[ ! -f "${GLADIATOR_Q2_BASEDIR}/baseq2/pak0.pak" ]] && require_q2+=("pak0.pak inside baseq2")
		if [[ ${#require_q2[@]} -gt 0 ]]; then
			printf "%s\n" "$(yellow "Quake II assets incomplete:")"
			for entry in "${require_q2[@]}"; do
				printf " - %s\n" "$entry"
			done
		else
			printf "%s %s\n" "$(green "Quake II assets detected at")" "${GLADIATOR_Q2_BASEDIR}"
		fi
	fi
	if [[ -z "${GLADIATOR_Q2_DEDICATED_SERVER:-}" ]]; then
		printf "%s\n" "$(yellow "GLADIATOR_Q2_DEDICATED_SERVER is unset; headless Quake II parity will skip.")"
	elif [[ ! -x "${GLADIATOR_Q2_DEDICATED_SERVER}" ]]; then
		printf "%s\n" "$(yellow "Quake II dedicated server missing or not executable: ${GLADIATOR_Q2_DEDICATED_SERVER}")"
	else
		printf "%s %s\n" "$(green "Quake II dedicated server executable:")" "${GLADIATOR_Q2_DEDICATED_SERVER}"
	fi
else
	printf "%s\n" "Headless Quake II parity assets not checked (set GLADIATOR_Q2_BASEDIR and GLADIATOR_Q2_DEDICATED_SERVER to validate)."
fi
