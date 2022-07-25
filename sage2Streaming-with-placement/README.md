# Prerequisites
  * boost 1.55

# To run

decklinkcapture -h

Usage: declinkcapture -d <card id> -m <mode id> [OPTIONS]

    -d <card id>
    -m <mode id>
    -i <video input number>
    -p <pixelformat>
            0:  8 bit YUV (4:2:2) (default)
            1:  10 bit YUV (4:2:2)
            2:  10 bit RGB (4:4:4)
    -t <format>          Print timecode
            rp188:  RP 188
            vitc:  VITC
            serial:  Serial Timecode
    -f <filename>        Filename raw video will be written to
    -a <filename>        Filename raw audio will be written to
    -o <IP address:port> Which SAGE2 server to connect to
    -c <channels>        Audio Channels (2, 8 or 16 - default is 2)
    -s <depth>           Audio Sample Depth (16 or 32 - default is 32)
    -n <frames>          Number of frames to capture (default is unlimited)
    -3                   Capture Stereoscopic 3D (Requires 3D Hardware support)
    -u <SAM IP>          stream using SAM (audio), 127.0.0.1 by default
    -j                   physical inputs for SAM inputs (audio)
    -v                   stream using SAGE (video)
    -y                   apply deinterlacing filter
    
Example: decklinkcapture -d 0 -m 7 -i 1 -v -o 131.193.183.199:443
