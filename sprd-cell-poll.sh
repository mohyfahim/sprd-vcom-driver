#!/bin/sh
# Example periodic radio snapshot. Output goes to stdout/journal.
set -eu

AT="${AT_TOOL:-/usr/local/bin/sprd-at-tty}"
DEV="${SPRD_AT_DEVICE:-/dev/sprd-at}"

exec "$AT" -d "$DEV" \
	'AT+COPS?' \
	'AT+CSQ' \
	'AT+CEREG=2' \
	'AT+CEREG?' \
	'AT+CREG=2' \
	'AT+CREG?'
