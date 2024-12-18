all: fusemake
.PHONY: all

out/%.o: src/%.c $(shell find src -name '*.h') out/string_defs.h
	gcc -c $< -o $@ $(FLAGS) -Wall $$(pkg-config --libs --cflags fuse3)

fusemake: $(patsubst src/%.c,out/%.o,$(shell find src -name '*.c'))
	gcc $^ -o $@ $(FLAGS) -Wall $$(pkg-config --libs --cflags fuse3)

out/string_defs.h: test/make.sh usage.txt
	echo "const unsigned char skeleton_builder[] = {" > "$@"
	sed '/case/,/esac/{ /case\|esac/!d }' "$<" | xxd -i >> "$@"
	echo ",0};" >> "$@"
	echo "const unsigned char usage[] = {" >> "$@"
	cat usage.txt | xxd -i >> "$@"
	echo ",0};" >> "$@"
