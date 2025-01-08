build:
	clang main.c -o bin/main.exe -lole32 -lmfplat -lwmvcore -lmfuuid
run:
	./bin/main.exe