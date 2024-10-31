all: fusemake.c
	gcc -Wall $$(pkg-config --libs --cflags fuse3) $< -o fusemake
