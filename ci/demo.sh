#!/usr/bin/env bash
# Prints something colourful so a screenshot shows the terminal actually works.
printf '\033[1mkgraphical-term\033[0m on %s\n\n' "$(uname -sr)"

printf 'ANSI 16:   '
for i in 0 1 2 3 4 5 6 7; do printf '\033[4%dm  \033[0m' "$i"; done
for i in 0 1 2 3 4 5 6 7; do printf '\033[10%dm  \033[0m' "$i"; done
printf '\n'

printf '256 colour: '
for i in $(seq 16 51); do printf '\033[48;5;%dm \033[0m' "$i"; done
printf '\n'

printf 'Truecolor: '
for i in $(seq 0 4 255); do printf '\033[48;2;%d;80;%dm \033[0m' "$i" "$((255 - i))"; done
printf '\n\n'

printf '\033[1mbold\033[0m  \033[3mitalic\033[0m  \033[4munderline\033[0m  \033[9mstrike\033[0m  \033[7mreverse\033[0m  \033[31mred\033[0m  \033[32mgreen\033[0m  \033[34mblue\033[0m\n'
printf 'Unicode: ünïcödé → ★ ╔═╗ ║ ║ ╚═╝ 😀 🚀\n\n'

ls --color=always -l /etc | head -n 8
echo
echo 'The quick brown fox jumps over the lazy dog. 0123456789'
sleep 60
