all: fusemake pa
.PHONY: all

out/src/%.c: src/%-types.txt src/%-decl.h src/%-impl.h
	mkdir -p $(@D)
	rm $@ || true
	for i in $$(cat src/$*-types.txt); do \
		echo "#ifndef $*_$$i" > out/src/$*-$$i.h; \
		echo "#define $*_$$i" >> out/src/$*-$$i.h; \
		echo "#define $*_T $$i" >> out/src/$*-$$i.h; \
		echo "#include \"$*-decl.h\"" >> out/src/$*-$$i.h; \
		echo "#endif" >> out/src/$*-$$i.h; \
		echo "#define $*_T $$i" >> $@; \
		echo "#include \"$*-impl.h\"" >> $@; \
	done

out/%.o: %.c $(shell find src -name '*.h') out/string_defs.h out/src/list.c out/src/pool.c
	mkdir -p $(@D)
	gcc -Isrc -Iout/src -c $< -o $@ $(FLAGS) -Wall $$(pkg-config --libs --cflags fuse3)

fusemake: $(patsubst %.c,out/%.o,$(shell find src -name '*.c')) out/out/src/list.o out/out/src/pool.o
	gcc $^ -o $@ $(FLAGS) -Wall $$(pkg-config --libs --cflags fuse3)

pa: project-actions.c
	gcc $^ -o $@ $(FLAGS) -Wall

out/string_defs.h: test/make.sh usage.txt
	mkdir -p $(@D)
	echo "const unsigned char usage[] = {" > $@
	cat usage.txt | xxd -i >> $@
	echo ",0};" >> $@
