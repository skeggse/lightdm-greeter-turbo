build: greeter.c
	gcc -std=c99 -Wall -Wno-parentheses \
	  -I/usr/include/cairo -I/usr/lib/glib-2.0/include -I/usr/include/glib-2.0 -I/usr/include/pango-1.0 \
	  greeter.c -o greeter \
	  -lm -lxcb -lcairo -lpango-1.0 -lpangocairo-1.0

clean:
	rm -f greeter
