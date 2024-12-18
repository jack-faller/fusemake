#!/bin/sh
set -e
RECUR="$0" OUT="$1"
depend_one () { mkdir -p "$(dirname "$1")"; "$RECUR" "$1"; echo "$1"; }
which fusemake 2> /dev/null || fusemake () # Backup version of fusemake.
{ if [ "$#" = 1 ]; then while read -r dep; do depend_one "$dep"; done
  else shift; for dep in "$@"; do depend_one "$dep"; done; fi }
depend () { fusemake --depend "$@"; }
run () { xargs --verbose -d'\n' "$@"; }
rewrap () { sed "s#$1\(.*\)$2#$3\1$4#"; } # Rewrap a b c d maps a(.*)b to c\1d.
basic () { echo "$OUT" | rewrap "$1" "$2" "$3" "$4"; }

case "$OUT" in
	out/*.tab.h) basic "" .h "" .c         | depend ;;
	out/*.tab.c) basic out/ .tab.c "" .y | run bison -H -o "$OUT" ;;
	out/*.ll.c)  basic out/ .ll.c "" .l  | run flex -o "$OUT" ;;
	out/*.o)     basic out/ .o "" .c     | run gcc -o "$OUT" -c ;;
	out/*-main.c)
		FUNC="$(basic 'out/' '-main.c' '' _main)"
		echo "int $FUNC(int argc, char **argv);
int main(int argc, char **argv) { return $FUNC(argc, argv); }" > "$OUT"
		;;
	exe | test)
		find src -name '*.y' | rewrap "" .y out/ .tab.h | depend
		find src -name '*.l' | rewrap "" .y out/ .ll.c | depend
		depend "out/$OUT-main.c"
		(find src -name '*.c'; echo "out/$OUT-main.c") |
			rewrap "" .c out/ .o | depend | run gcc -o "$OUT"
		;;
	# Directory rules.
	out) mkdir -p out ;;
	out/*)
		[ -d "$(basic out/ '' '' '')" ] && mkdir "$OUT"
		;;
esac
