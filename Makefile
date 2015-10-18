LDLIBS := -lSDL -lSDL_image -lSDL_ttf -lvigor
CFLAGS := -Wall -Werror -I/usr/include/SDL -g -O0

default: sdl menu
sdl: sdl.o
menu: menu.o

run-demo: sdl
	DISPLAY=:0 ./sdl
run-menu: menu
	DISPLAY=:0 ./menu

run: run-menu
clean:
	rm -f sdl menu *.o
