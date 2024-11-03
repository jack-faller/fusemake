all: fusemake.c
	gcc $(FLAGS) -Wall $$(pkg-config --libs --cflags fuse3) $< -o fusemake
