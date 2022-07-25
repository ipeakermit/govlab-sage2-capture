
CC            = gcc
CXX           = g++
DEFINES       = 
CFLAGS        = $(DEFINES)
CXXFLAGS      = $(DEFINES)
INCPATH       = -Iinclude -I. #-I/home/sage/sage2/deps/ffmpeg-2.2.1/
LINK          = g++
LFLAGS        = 
# new version of websocketspp requires -lboost_chrono-mt
#LIBS          = $(SUBLIBS) -lm -ldl -L/usr/lib64 -lboost_thread -lboost_system -lssl -lcrypto -lboost_random -ljpeg -lpostproc -lswresample -lswscale -lavfilter -lavdevice -lavformat -lavcodec -lavutil -lrt -lpthread -lboost_chrono-mt
#LIBS          = $(SUBLIBS) -L/home/sage/sage2/deps/ffmpeg-2.2.1/lib/ -lm -ldl -L/usr/lib64 -lboost_thread -lboost_system -lssl -lcrypto -lboost_random -ljpeg -lpostproc -lswresample -lswscale -lavfilter -lavdevice -lavformat -lavcodec -lavutil -lrt -lpthread -lboost_chrono-mt
LIBS          = $(SUBLIBS) -L/usr/lib/x86_64-linux-gnu -lm -ldl -L/usr/lib64 -lboost_thread -lboost_system -lssl -lcrypto -lboost_random -ljpeg -lpostproc -lswresample -lswscale -lavfilter -lavdevice -lavformat -lavcodec -lavutil -lrt -lpthread -lboost_chrono
DEL_FILE      = rm -f
SYMLINK       = ln -f -s
DEL_DIR       = rmdir
MOVE          = mv -f
CHK_DIR_EXISTS= test -d
MKDIR         = mkdir -p

####### Output directory

OBJECTS_DIR   = ./

####### Files

SOURCES		= include/DeckLinkAPIDispatch.cpp \
				websocketio.cpp \
				decklinkcapture.cpp
OBJECTS		= DeckLinkAPIDispatch.o \
				websocketio.o \
				decklinkcapture.o
DESTDIR       = 
TARGET        = decklinkcapture

first: all
####### Implicit rules

.SUFFIXES: .o .c .cpp .cc .cxx .C

.cpp.o:
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o "$@" "$<"

.cc.o:
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o "$@" "$<"

.cxx.o:
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o "$@" "$<"

.C.o:
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o "$@" "$<"

.c.o:
	$(CC) -c $(CFLAGS) $(INCPATH) -o "$@" "$<"

####### Build rules

all: Makefile $(TARGET)

$(TARGET):  $(OBJECTS)  
	$(LINK) $(LFLAGS) -o $(TARGET) $(OBJECTS) $(OBJCOMP) $(LIBS)

clean:
	-$(DEL_FILE) $(OBJECTS)
	-$(DEL_FILE) *~ core *.core


####### Sub-libraries

distclean: clean
	-$(DEL_FILE) $(TARGET) 
	-$(DEL_FILE) test_websocket *.o


####### Compile

#decklinkcapture.o: decklinkcapture.cpp decklinkcapture.h \
#		ring.h
#	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o decklinkcapture.o decklinkcapture.cpp

DeckLinkAPIDispatch.o: include/DeckLinkAPIDispatch.cpp include/DeckLinkAPI.h \
		include/LinuxCOM.h \
		include/DeckLinkAPITypes.h \
		include/DeckLinkAPIModes.h \
		include/DeckLinkAPIDiscovery.h \
		include/DeckLinkAPIConfiguration.h \
		include/DeckLinkAPIDeckControl.h
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o DeckLinkAPIDispatch.o include/DeckLinkAPIDispatch.cpp

websocketio.o: websocketio.cpp websocketio.h
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o websocketio.o websocketio.cpp

decklinkcapture.o: decklinkcapture.cpp 
	$(CXX) -c $(CXXFLAGS) $(INCPATH) -o decklinkcapture.o decklinkcapture.cpp

test_websocket: test_websocket.o websocketio.o
	$(CXX) -o test_websocket test_websocket.o websocketio.o $(LIBS)

