FROM ubuntu:16.04

# use local apt proxy to cache package downloads
COPY 02proxy /etc/apt/apt.conf.d
COPY etc-apt-sources-list /etc/apt/sources.list

# https://stackoverflow.com/questions/62034545/dockerfile-how-to-set-apt-mirror-based-on-the-ubuntu-release
# broken for 16.04?
#RUN sed -i -e 's/http:\/\/archive\.ubuntu\.com\/ubuntu\//mirror:\/\/mirrors\.ubuntu\.com\/mirrors\.txt/' /etc/apt/sources.list

RUN apt update && apt dist-upgrade
RUN apt update && apt -y install libjpeg-dev libboost-dev libssl-dev libavformat-dev libswscale-dev libboost-thread-dev libboost-random-dev libpostproc-dev libavdevice-dev
RUN apt update && apt -y install make
RUN apt update && apt -y install g++
# Makefile: LIBS - add -L/usr/lib/x86_64-linux-gnu
# rm websocketio.o

COPY sage2Streaming-with-placement /root/src

WORKDIR /root/src

RUN make
