all: fusemake.c string_defs.h
	gcc $(FLAGS) -Wall $$(pkg-config --libs --cflags fuse3) $< -o fusemake

string_defs.h: test/make.sh usage.txt
	echo "const unsigned char skeleton_builder[] = {" > "$@"
	sed '/case/,/esac/{ /case\|esac/!d }' "$<" | xxd -i >> "$@"
	echo ",0};" >> "$@"
	echo "const unsigned char usage[] = {" >> "$@"
	cat usage.txt | xxd -i >> "$@"
	echo ",0};" >> "$@"
