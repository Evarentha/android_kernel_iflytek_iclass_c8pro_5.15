#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
# Apply SPRD ADD/DEL/MOD/VAL/STR fragments using the kernel config helper.
set -e

if [ "$#" -ne 2 ]; then
	echo "Usage: $0 CONFIG FRAGMENT" >&2
	exit 1
fi

config_file=$1
fragment=$2
config_tool=$(dirname "$(readlink -f "$0")")/../config

while IFS= read -r line || [ -n "$line" ]; do
	line=${line%$'\r'}
	case "$line" in
	''|'#'*) continue ;;
	esac

	operation=${line%%:*}
	setting=${line#*:}
	if [[ "$setting" != CONFIG_* ]]; then
		echo "$fragment: invalid config directive: $line" >&2
		exit 1
	fi
	setting=${setting#CONFIG_}

	case "$operation" in
	ADD) "$config_tool" --file "$config_file" --enable "$setting" ;;
	DEL) "$config_tool" --file "$config_file" --disable "$setting" ;;
	MOD) "$config_tool" --file "$config_file" --module "$setting" ;;
	VAL|STR)
		if [[ "$setting" != *=* ]]; then
			echo "$fragment: missing value: $line" >&2
			exit 1
		fi
		symbol=${setting%%=*}
		value=${setting#*=}
		if [ "$operation" = VAL ]; then
			"$config_tool" --file "$config_file" --set-val "$symbol" "$value"
		else
			"$config_tool" --file "$config_file" --set-str "$symbol" "$value"
		fi
		;;
	*)
		echo "$fragment: unknown operation: $operation" >&2
		exit 1
		;;
	esac
done < "$fragment"
