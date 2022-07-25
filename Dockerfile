FROM ubuntu:16.04

COPY 02proxy /etc/apt/apt.conf.d

RUN apt update && apt dist-upgrade
RUN apt update && apt -y install libjpeg-dev libboost-dev libssl-dev libavformat-dev libswscale-dev libboost-thread-dev libboost-random-dev libpostproc-dev libavdevice-dev
RUN apt update && apt -y install make
# Makefile: LIBS - add -L/usr/lib/x86_64-linux-gnu
# rm websocketio.o

COPY sage2Streaming-with-placement src

WORKDIR /root/src

RUN make
