# windows-video

Hacking on video stuff for Windows using C.

## system dependencies

1. Make
2. gcc/clang
3. ffmpeg (for video generation and testing)

## usage

Step 1: generate a H264 video to work with

```bash
ffmpeg -f lavfi -t 5 -i testsrc=r=30:s=1920x1080 -c:v h264_nvenc video.h264
```

Step 2: run the program

```bash
make build
make run
```

Step 3: get the generated raw video to get back to a viewable format

```bash
ffmpeg -f rawvideo -pix_fmt nv12 -s 1920x1080 -i output.raw -c:v h264_nvenc output.mp4
```

## notes

`make build` runs the ffmpeg.c program to generate a raw video file in RGBA pixel format from the input H264 video. I also spent some time writing a decoder in windows using Media Foundation. That shit does not work. It is a terrible API to do video stuff. I admit that it could partly be a skill use but who the hell designs an API like this.

Anyway, if you had to compile the windows program, you could do it by:

```bash
clang main.c -o bin/main.exe -lole32 -lmfplat -lwmvcore -lmfuuid -lWs2_32
```

### acknowledgements

created by Vivek Nathani ([@viveknathani\_](https://twitter.com/viveknathani_)), licensed under the [MIT License](./LICENSE).
