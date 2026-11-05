#!/bin/sh
# Шрифты панели (стиль Б) из docs/design/fonts в panel/main/fonts. Системный python: нужны PIL с raqm.
set -e
cd "$(dirname "$0")/../.."
F=docs/design/fonts
O=panel/main/fonts
mkdir -p $O
gen() { /usr/bin/python3 tools/fonts/lvfont.py --font $F/$1 --size $2 --range $3 --chars "$4" --name $5 -o $O/$5.c; }
gen BarlowCondensed-Medium.ttf 15 0x20-0x7E '‹›×±' bc_15
gen BarlowCondensed-SemiBold.ttf 17 0x20-0x7E '‹›×±' bc_17
gen BarlowCondensed-SemiBold.ttf 22 0x20-0x7E '‹›×±' bc_22
gen BarlowCondensed-SemiBold.ttf 32 0x20-0x5F '‹›' bc_32
gen BarlowCondensed-SemiBold.ttf 58 0x20-0x5F '‹›' bc_58
