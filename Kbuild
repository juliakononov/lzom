ccflags-y := 	-Wall					\
		-Wextra					\
		-Wno-missing-field-initializers		\
		-Wno-unused-parameter			\
		-Wformat				\
		-O2					\
		-std=gnu18				\
		-g					\
		-Werror=format-security			\
		-Werror=implicit-function-declaration	\
		-Ilzom/include

lzom_module-y := module/lzom_module.o
lzom_module-y += lzom/lzom_compress.o
lzom_module-y += lzom/lzom_decompress_safe.o
lzom_module-y += lzom/lzom_sg_helpers.o


obj-m := lzom_module.o