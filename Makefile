all: fusemake pa
.PHONY: all

out/%.o: src/%.c $(shell find src -name '*.h') out/string_defs.h
	mkdir -p $(@D)
	gcc -c $< -o $@ $(FLAGS) -Wall $$(pkg-config --libs --cflags fuse3)

fusemake: $(patsubst src/%.c,out/%.o,$(shell find src -name '*.c'))
	mkdir -p $(@D)
	gcc $^ -o $@ $(FLAGS) -Wall $$(pkg-config --libs --cflags fuse3)

pa: project-actions.c
	gcc $^ -o $@ $(FLAGS) -Wall

out/string_defs.h: test/make.sh usage.txt
	mkdir -p $(@D)
	echo "const unsigned char usage[] = {" > "$@"
	cat usage.txt | xxd -i >> "$@"
	echo ",0};" >> "$@"
