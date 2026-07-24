#!/bin/sh
# Reproduce the whole verification: a moving source, a clean rendition, a 40 kbps rendition,
# and a broken 15 kbps deblocking-off rendition — then score all three with ggw_streamqc.
# Needs ffmpeg. Everything it makes is throwaway (see .gitignore).
set -e
cd "$(dirname "$0")"
W=320; H=240; N=24
[ -x ./ggw_streamqc ] || g++ -std=c++17 -O2 ggw_streamqc.cpp -o ggw_streamqc

ffmpeg -v error -y -f lavfi -i "mandelbrot=size=${W}x${H}:rate=12" -frames:v $N -pix_fmt yuv420p src.yuv

ffmpeg -v error -y -f rawvideo -pix_fmt yuv420p -s ${W}x${H} -r 12 -i src.yuv -c:v libx264 -crf 12 -f mp4 ref.mp4
ffmpeg -v error -y -i ref.mp4 -pix_fmt yuv420p -s ${W}x${H} ref.yuv

ffmpeg -v error -y -f rawvideo -pix_fmt yuv420p -s ${W}x${H} -r 12 -i src.yuv \
  -c:v libx264 -b:v 40k -maxrate 40k -bufsize 20k -preset veryfast -f mp4 deg.mp4
ffmpeg -v error -y -i deg.mp4 -pix_fmt yuv420p -s ${W}x${H} deg.yuv

ffmpeg -v error -y -f rawvideo -pix_fmt yuv420p -s ${W}x${H} -r 12 -i src.yuv \
  -c:v libx264 -b:v 15k -maxrate 15k -bufsize 8k -preset veryfast \
  -x264-params "deblock=-6,-6:no-deblock=1" -f mp4 broken.mp4
ffmpeg -v error -y -i broken.mp4 -pix_fmt yuv420p -s ${W}x${H} broken.yuv

echo "=== self-test ===";                 ./ggw_streamqc --selftest
echo; echo "=== 40 kbps rendition ===";    ./ggw_streamqc --ref ref.yuv --deg deg.yuv    --w $W --h $H
echo; echo "=== 15 kbps, deblock off ==="; ./ggw_streamqc --ref ref.yuv --deg broken.yuv --w $W --h $H
