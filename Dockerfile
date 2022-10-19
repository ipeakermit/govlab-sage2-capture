FROM ubuntu:16.04

# use local apt proxy to cache package downloads
COPY 02proxy /etc/apt/apt.conf.d
COPY sources.list /etc/apt/sources.list

# https://stackoverflow.com/questions/62034545/dockerfile-how-to-set-apt-mirror-based-on-the-ubuntu-release
# broken for 16.04?
#RUN sed -i -e 's/http:\/\/archive\.ubuntu\.com\/ubuntu\//mirror:\/\/mirrors\.ubuntu\.com\/mirrors\.txt/' /etc/apt/sources.list

RUN apt update && apt -y dist-upgrade
RUN apt update && apt -y install libjpeg-dev libboost-dev libssl-dev libavformat-dev libswscale-dev libboost-thread-dev libboost-random-dev libpostproc-dev libavdevice-dev
RUN apt update && apt -y install make
RUN apt update && apt -y install g++
# Makefile: LIBS - add -L/usr/lib/x86_64-linux-gnu
# rm websocketio.o

COPY sage2Streaming-with-placement /root/src

WORKDIR /root/src

RUN make

# black magic design drivers
WORKDIR /root
#COPY Blackmagic_Desktop_Video_Linux_10.9.12.tar /root/blackmagic.tar
COPY Blackmagic_Desktop_Video_Linux_12.4.tar.gz /root/blackmagic.tar.gz
RUN tar zxfp /root/blackmagic.tar.gz
#WORKDIR /root/Blackmagic_Desktop_Video_Linux_10.9.12/deb/x86_64
WORKDIR /root/Blackmagic_Desktop_Video_Linux_12.4/deb/x86_64
RUN apt update && apt install -y libatk1.0-0 
RUN apt update && apt install -y dkms
RUN apt update && apt install -y linux-headers-generic
#RUN apt update && apt install -y linux-headers-4.4.0-134-generic
RUN apt update && apt install -y linux-headers-5.15.0-46-generic
RUN dpkg -i desktopvideo_*.deb

WORKDIR /root
COPY cmd cmd
