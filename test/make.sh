#!/bin/sh
set -e
OUT="$1"
RECUR="$0"
depend_one () { mkdir -p "$(dirname "$1")"; "$RECUR" "$1"; echo "$1"; }
depend () {
	fusemake --depend "$@" ||
		if [ "$#" = "0" ];
		then while read -r dep; do depend_one "$dep"; done
		else for dep in "$@"; do depend_one "$dep"; done; fi
}
run () { xargs --verbose -d'\n' "$@"; }
rewrap () { sed "s#$1\(.*\)$2#$3\1$4#"; } # rewrap a b c d maps a(.*)b to c\1d
basic () { echo "$OUT" | rewrap "$1" "$2" "$3" "$4"; }

case "$OUT" in
	build/*.tab.h) basic "" .h "" .c         | depend ;;
	build/*.tab.c) basic build/ .tab.c "" .y | run bison -H -o "$OUT" ;;
	build/*.ll.c)  basic build/ .ll.c "" .l  | run flex -o "$OUT" ;;
	build/*.o)     basic build/ .o "" .c     | run gcc -o "$OUT" -c ;;
	build/*-main.c)
		FUNC="$(basic 'build/' '-main.c' '' _main)"
		echo "int $FUNC(int argc, char **argv);
int main(int argc, char **argv) { return $FUNC(argc, argv); }" > "$OUT"
		;;
	exe | test)
		find src -name '*.y' | rewrap "" .y build/ .tab.h | depend
		find src -name '*.l' | rewrap "" .y build/ .ll.c | depend
		depend "build/$OUT-main.c"
		(find src -name '*.c'; echo "build/$OUT-main.c") |
			rewrap "" .c build/ .o | depend | run gcc -o "$OUT"
		;;
	# Directory rules.
	build) mkdir -p build ;;
	build/*)
		[ -d "$(basic build/ '' '' '')" ] && mkdir "$OUT"
		;;
esac
