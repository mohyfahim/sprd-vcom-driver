#!/bin/sh
# SPDX-License-Identifier: GPL-2.0
# Example periodic radio snapshot. Output is written to stdout/the journal.
set -eu

at_tool=${AT_TOOL:-/usr/local/bin/sprd-at-tty}
device=${SPRD_AT_DEVICE:-}

if [ -n "$device" ]; then
	exec "$at_tool" -d "$device" \
		'AT+COPS?' 'AT+CSQ' 'AT+CEREG=2' 'AT+CEREG?' \
		'AT+CREG=2' 'AT+CREG?'
fi

exec "$at_tool" \
	'AT+COPS?' 'AT+CSQ' 'AT+CEREG=2' 'AT+CEREG?' \
	'AT+CREG=2' 'AT+CREG?'
