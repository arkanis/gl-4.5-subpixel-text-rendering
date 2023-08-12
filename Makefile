# This makefile builds the demo programs and their dependencies.
# 
# Condition for Windows taken from http://stackoverflow.com/a/12099167 by Trevor Robinson.
# make -p lists all defined variables and implicit rules. Useful to see what we start out with.
# $(warning $(VAR)) is usefull to output the value of a variable for debugging.


# Setup implicit rule to build object files from *.c files
CC = gcc
CPPFLAGS = -Ideps/include
CFLAGS = -std=c99 -Werror -Wall -Wextra -Wno-unused-parameter -g

# Dependencies and custom variables for the "main" binary
main: CFLAGS += $(SDL_CFLAGS)
main: LDLIBS += $(SDL_LDLIBS)
main: deps/libSDL2.a

# Clean all files in the .gitignore list, ensures that the ignore file is properly maintained.
clean:
	xargs -a .gitignore -t -I FILE sh -c "rm -rf FILE"


#
# Platform specific scripts to download and use SDL
#
ifeq ($(OS),Windows_NT)

# On Windows download the MinGW development files and extract the static libraries and headers from it

# Compiler flags for SDL, taken from libSDL2-devel\SDL2-2.24.0\x86_64-w64-mingw32\lib\pkgconfig\sdl2.pc
SDL_CFLAGS += -Dmain=SDL_main
SDL_LDLIBS += -Ldeps -lmingw32 -lSDL2main -lSDL2 -mwindows -Wl,--dynamicbase -Wl,--nxcompat -Wl,--high-entropy-va -lm -ldinput8 -ldxguid -ldxerr8 -luser32 -lgdi32 -lwinmm -limm32 -lole32 -loleaut32 -lshell32 -lsetupapi -lversion -luuid  -static-libgcc

deps/libSDL2-devel.tar.gz:
	wget https://www.libsdl.org/release/SDL2-devel-2.26.5-mingw.tar.gz -O deps/libSDL2-devel.tar.gz

deps/libSDL2.a: deps/libSDL2-devel.tar.gz
	mkdir -p deps/SDL2
	tar -xaf deps/libSDL2-devel.tar.gz -C deps/SDL2 --strip-components 1
	mv deps/SDL2/x86_64-w64-mingw32/lib/libSDL2.a deps/SDL2/x86_64-w64-mingw32/lib/libSDL2main.a deps
	mkdir -p deps/include/SDL
	mv deps/SDL2/x86_64-w64-mingw32/include/SDL2/*.h deps/include/SDL
	rm -rf deps/SDL2
	touch deps/libSDL2.a

else

# On Linux download and build SDL2 as static library
SDL_CFLAGS = -pthread
SDL_LDLIBS = -lm -ldl -lpthread -lrt -lOpenGL  # taken from line "dependency_libs" in deps/SDL2/build/libSDL2.la, -lOpenGL added by myself

deps/libSDL2.tar.gz:
	wget https://github.com/libsdl-org/SDL/releases/download/release-2.26.5/SDL2-2.26.5.tar.gz -O deps/libSDL2.tar.gz

deps/libSDL2.a: deps/libSDL2.tar.gz
	mkdir -p deps/SDL2
	tar -xaf deps/libSDL2.tar.gz -C deps/SDL2 --strip-components 1
	cd deps/SDL2;  ./configure --disable-shared
	cd deps/SDL2;  nice make -j 16
	mv deps/SDL2/build/.libs/libSDL2.a deps/
	mkdir -p deps/include/SDL
	mv deps/SDL2/include/*.h deps/include/SDL
	rm -rf deps/SDL2

endif