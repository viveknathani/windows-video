build:
	clang ffmpeg.c -o bin/ffmpeg.exe -ID:/ffmpeg/include -LD:/ffmpeg/lib -lavcodec -lavformat -lavutil -lswscale -lswresample -lavdevice -lavfilter
run:
	./bin/ffmpeg.exe
	