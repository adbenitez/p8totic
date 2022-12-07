CFLAGS=-Wall -Wextra -O3

all: cli wasm

wasm: p8totic.c
	emcc -s WASM=1 -s ALLOW_MEMORY_GROWTH=1 -s EXPORTED_FUNCTIONS='["_p8totic","_tictopng","_malloc","_free"]' -s EXPORTED_RUNTIME_METHODS='["ccall","cwrap"]' $(CFLAGS) p8totic.c -o ../public/p8totic.js

cli: p8totic.c
ifneq ("$(wildcard /bin/*.exe)","")
	gcc $(CFLAGS) p8totic.c -o p8totic -Wl,--nxcompat -Wl,-Bstatic,--whole-archive -lwinpthread -Wl,--no-whole-archive
else
	gcc $(CFLAGS) p8totic.c -o p8totic
endif

clean:
	rm ../public/p8totic.js ../public/p8totic.wasm p8totic p8totic.exe 2>/dev/null || true
