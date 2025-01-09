build:
	clang main.c -o bin/main.exe -lole32 -lmfplat -lwmvcore -lmfuuid -lWs2_32
run:
	./bin/main.exe