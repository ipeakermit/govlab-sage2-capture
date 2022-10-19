#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <jpeglib.h>

#include "websocketio.h"
#include "decklinkcapture.h"

//Decklink code before main

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctime>

#include <iostream>
#include <fstream>

#include "DeckLinkAPI.h"
#include "decklinkcapture.h"

extern "C" {
    #include <libavformat/avformat.h>
    #include <libavcodec/avcodec.h>
    #include <libswscale/swscale.h>
    #include <libavutil/mathematics.h>
}

struct SwsContext *img_convert_toyuv420p;
AVPicture picture_curr, picture_yuv420p;

using namespace std;

bool got_request = false;
void sendFrame();
void serializeFrame();


// FFMPEG
#if defined(DEINTERLACING)

AVPicture *picture_prev, *picture_next, *picture_curr;
struct SwsContext *img_convert_topacked;
struct SwsContext *img_convert_toplanar;
struct SwsContext *img_convert_yuvtorgb;
#endif

struct SwsContext *img_convert_yuvtorgb;
// SAGE
int  useSAGE = 0;
int  useDEINT = 0; //de-interlacing
int  useSAM  = 0;
int  sageW, sageH;
std::string sageX, sageY, sageSX, sageSY;
bool sagePlacement = false;
double sageFPS;
bool isFirstFrame = true;
bool benchmark = false;
bool useYUV422 = false;
bool dontWait = false;
bool debug = false;
bool useBlocks = false;
std::string sage2title = "Decklinkcapture";

// sage2 stuff
WebSocketIO* wsio;
std::string uniqueID;
void* frameBytes;

// SAM
#define AUDIO_TEMP 16384
int audio_temp[AUDIO_TEMP]; //32bit
//sam::StreamingAudioClient* m_sac = NULL;
int physicalInputs = 0;
char *samIP;

// if achieving less than threshold frame rate
//     then drop frames
#define DROP_THRESHOLD 0.95

// Decklink
pthread_mutex_t			sleepMutex;
pthread_cond_t			sleepCond;
int				videoOutputFile = -1;
int				audioOutputFile = -1;
int				card = 0;
IDeckLink 			*deckLink;
IDeckLinkInput			*deckLinkInput;
IDeckLinkDisplayModeIterator	*displayModeIterator;

static BMDTimecodeFormat	g_timecodeFormat = 0;
static int			g_videoModeIndex = -1;
static int			g_audioChannels = 2;
static int			g_audioSampleDepth = 32;
const char *			g_videoOutputFile = NULL;
const char *			g_audioOutputFile = NULL;
const char *       		sage2Server = NULL;
static int			g_maxFrames = -1;
static unsigned long 		frameCount = 0;
BMDTimeValue frameRateDuration, frameRateScale;

// SAGE2 block size
const int maxSize = 512;

double getTime();
void startTimer();
struct timeval tv_start;
#if defined(DEINTERLACING)
////////////////////////////////////////////////////////////////////////////////
// FFMPEG deinterlacing
////////////////////////////////////////////////////////////////////////////////
// ==================================
AVFrame *alloc_picture(enum PixelFormat pix_fmt, int width, int height)
{
    AVFrame *picture;
    uint8_t *picture_buf;
    int size;
    picture = avcodec_alloc_frame();
    if (!picture)
        return NULL;
    size = avpicture_get_size(pix_fmt, width, height);
    picture_buf = (uint8_t*)av_malloc(size);
    fprintf(stderr, "Allocating picture: %d bytes [%dx%d]\n", size, width, height);
    if (!picture_buf) {
        av_free(picture);
        return NULL;
    }
    avpicture_fill((AVPicture *)picture, picture_buf, pix_fmt, width, height);
    return picture;
}

	//< packed YUV 4:2:2, 16bpp, Y0 Cb Y1 Cr
#define SAGE_YUV   AV_PIX_FMT_YUYV422

	//< packed YUV 4:2:2, 16bpp, Cb Y0 Cr Y1
//#define SAGE_YUV   PIX_FMT_UYVY422

////////////////////////////////////////////////////////////////////////////////
#endif

DeckLinkCaptureDelegate::DeckLinkCaptureDelegate() : m_refCount(0)
{
	pthread_mutex_init(&m_mutex, NULL);
}

DeckLinkCaptureDelegate::~DeckLinkCaptureDelegate()
{
	pthread_mutex_destroy(&m_mutex);
}

ULONG DeckLinkCaptureDelegate::AddRef(void)
{
	pthread_mutex_lock(&m_mutex);
		m_refCount++;
	pthread_mutex_unlock(&m_mutex);

	return (ULONG)m_refCount;
}

ULONG DeckLinkCaptureDelegate::Release(void)
{
	pthread_mutex_lock(&m_mutex);
		m_refCount--;
	pthread_mutex_unlock(&m_mutex);

	if (m_refCount == 0)
	{
		delete this;
		return 0;
	}

	return (ULONG)m_refCount;
}

void decklinkQuit()
{
	pthread_cond_signal(&sleepCond);
	//if (m_sac) delete m_sac;
}

HRESULT DeckLinkCaptureDelegate::VideoInputFrameArrived(IDeckLinkVideoInputFrame* videoFrame, IDeckLinkAudioInputPacket* audioFrame)
{
	IDeckLinkVideoFrame*	                rightEyeFrame = NULL;
	IDeckLinkVideoFrame3DExtensions*        threeDExtensions = NULL;
	void*					audioFrameBytes;
	static int warmup = (int)sageFPS; // 1 sec of frames
	static BMDTimeValue startoftime;
	static int skipf = 0;
	static double lastframetime = 0.0;

	// Handle Video Frame
	if(videoFrame)
	{

#if 0
		if (warmup > 0) {
		    warmup--;
		    fprintf(stderr, "Warmup: frame %03d\r", warmup);
		    return S_OK;
		}
		if (warmup == 0) {
		    fprintf(stderr, "\nWarmup done\n");
		    warmup--;
		}
#endif

		if (frameCount==0) {
			BMDTimeValue hframedur;
			HRESULT h = videoFrame->GetHardwareReferenceTimestamp(frameRateScale, &startoftime, &hframedur);
			lastframetime = getTime();
		}

		if (videoFrame->GetFlags() & bmdFrameHasNoInputSource)
		{
			fprintf(stderr, "Frame received (#%lu) - No input signal detected\n", frameCount);
        }
		else
		{
			if (useSAGE && sage2Server!=NULL) {
                if(isFirstFrame) {
                    startTimer();
                    isFirstFrame = false;
                }
				BMDTimeScale htimeScale = frameRateScale;
				BMDTimeValue hframeTime;
				BMDTimeValue hframeDuration;
				HRESULT h = videoFrame->GetHardwareReferenceTimestamp(htimeScale, &hframeTime, &hframeDuration);
				if (h == S_OK) {
					double frametime = (double)(hframeTime-startoftime) / (double)hframeDuration;
					double blabla = getTime();
                    double instantfps = 1000000.0/(getTime()-lastframetime);
					// TO BE CHECKED
					//if ( ((int)frametime) > frameCount) { skipf = 1; frameCount++; }
					//if ( instantfps <= (sageFPS*DROP_THRESHOLD) ) { skipf = 1; frameCount++; }   // if lower than 95% of target FPS
                    lastframetime = getTime();
				} else {
					BMDTimeValue frameTime, frameDuration;
					videoFrame->GetStreamTime(&frameTime, &frameDuration, frameRateScale);

					printf("SOFT Time: %10ld %10ld %10ld", frameTime, frameDuration, frameRateScale);
                }

				// Get the pixels from the capture
				videoFrame->GetBytes(&frameBytes);

				if (useDEINT) {
			  printf("useDEINT\n");
#if defined(DEINTERLACING)
					int ret;

					// Put data into frame
					memcpy(picture_curr->data[0], frameBytes, sageW*sageH*2);
					// Convert to planar format
					//sws_scale(img_convert_toplanar, picture_curr->data, picture_curr->linesize, 0,
                                                                    sageH, picture_next->data, picture_next->linesize);
					// Deinterlacing (in place)
					ret = avpicture_deinterlace(picture_next, picture_next, AV_PIX_FMT_YUV422P, sageW, sageH);
					if (ret!=0)
						sage::printLog("Deinterlace error %d", ret);

					// Convert to packed format
					//sws_scale(img_convert_topacked, picture_next->data, picture_next->linesize, 0,
                                                                    sageH, picture_prev->data, picture_prev->linesize);
                    //sws_scale(img_convert_toyuv420p, picture_prev.data, picture_prev.linesize, 0,
                                                                    sageH, picture_yuv420p.data, picture_yuv420p.linesize);
#else
					// Pass the pixels to SAGE
                    // Convert from YUV422P to RGB
                    memcpy(picture_curr.data[0], frameBytes, sageW*sageH*2);
                    sws_scale(img_convert_toyuv420p, picture_curr.data, picture_curr.linesize, 0,
                                                                    sageH, picture_yuv420p.data, picture_yuv420p.linesize);

#endif
				} else {
					// Pass the pixels to SAGE

					// int localskip = 0;
					// if (sageFPS >= 50.0) {                // if 50p or 60p rate
					// 	if ( (frameCount%2) == 0 ) {  // skip every other frame
					// 	//	localskip = 1;
					// 	}
					// }
					// if (!localskip) {
			  //if (debug && benchmark) printf("sage frame\n");
                        avpicture_fill((AVPicture*)&picture_curr, (unsigned char*)frameBytes, AV_PIX_FMT_UYVY422, sageW, sageH);
		        if (!useYUV422) {
			  //if (debug) { printf("scale\n"); }
                          sws_scale(img_convert_toyuv420p, picture_curr.data, picture_curr.linesize, 0,
                                    sageH, picture_yuv420p.data, picture_yuv420p.linesize);
                        }
                    // }
                }
                //serializeFrame();
                //printf("Got frame\n");
                if (dontWait || got_request) {
                	sendFrame();
                }
			}

	}
	frameCount++;

    if (g_maxFrames > 0 && frameCount >= g_maxFrames)
	{
        pthread_cond_signal(&sleepCond);
    }
}


	// Handle Audio Frame
	if (audioFrame)
	{
		if (useSAM && ! physicalInputs ) {
			int gotFrames = audioFrame->GetSampleFrameCount();
			audioFrame->GetBytes(&audioFrameBytes);
			unsigned int len =  gotFrames * g_audioChannels * (g_audioSampleDepth / 8);
			unsigned int putin =  len;
		}
		else {
			if (audioOutputFile != -1)
			{
				audioFrame->GetBytes(&audioFrameBytes);
				write(audioOutputFile, audioFrameBytes, audioFrame->GetSampleFrameCount() * g_audioChannels * (g_audioSampleDepth / 8));
			}
		}
	}



	skipf = 0;

    return S_OK;
}

HRESULT DeckLinkCaptureDelegate::VideoInputFormatChanged(BMDVideoInputFormatChangedEvents events, IDeckLinkDisplayMode *mode, BMDDetectedVideoInputFormatFlags)
{
    fprintf(stderr, "VideoInputFormatChanged VideoInputFormatChanged VideoInputFormatChanged\n");
    fprintf(stderr, "VideoInputFormatChanged VideoInputFormatChanged VideoInputFormatChanged\n");
    fprintf(stderr, "VideoInputFormatChanged VideoInputFormatChanged VideoInputFormatChanged\n");
    return S_OK;
}
int usage(int status)
{
	HRESULT result;
	IDeckLinkDisplayMode *displayMode;
	int displayModeCount = 0;

	fprintf(stderr,
		"Usage: declinkcapture -d <card id> -m <mode id> [OPTIONS]\n"
		"\n"
		"    -d <card id>:\n");

        IDeckLinkIterator *di = CreateDeckLinkIteratorInstance();
	IDeckLink *dl;
	int dnum = 0;
        while (di->Next(&dl) == S_OK)
        {
		const char *deviceNameString = NULL;
		int  result = dl->GetModelName(&deviceNameString);
		if (result == S_OK)
		{
			fprintf(stderr, "\t device %d [%s]\n", dnum, deviceNameString);
		}
		dl->Release();
		dnum++;
	}


	fprintf(stderr,
		"    -m <mode id>:\n"
	);

    while (displayModeIterator->Next(&displayMode) == S_OK)
    {
        char *displayModeString = NULL;

        result = displayMode->GetName((const char **) &displayModeString);
        if (result == S_OK)
        {
		BMDTimeValue iframeRateDuration, iframeRateScale;
		displayMode->GetFrameRate(&iframeRateDuration, &iframeRateScale);

		fprintf(stderr, "        %2d:  %-20s \t %li x %li \t %g FPS\n",
			displayModeCount, displayModeString, displayMode->GetWidth(), displayMode->GetHeight(), (double)iframeRateScale / (double)iframeRateDuration);

		free(displayModeString);
		displayModeCount++;
        }

        // Release the IDeckLinkDisplayMode object to prevent a leak
        displayMode->Release();
    }

	fprintf(stderr,
		"    -i <video input number>\n"
		"    -p <pixelformat>\n"
		"         0:  8 bit YUV (4:2:2) (default)\n"
		"         1:  10 bit YUV (4:2:2)\n"
		"         2:  10 bit RGB (4:4:4)\n"
		"    -t <format>          Print timecode\n"
		"     rp188:  RP 188\n"
		"      vitc:  VITC\n"
		"    serial:  Serial Timecode\n"
		"    -f <filename>        Filename raw video will be written to\n"
		"    -a <filename>        Filename raw audio will be written to\n"
        "    -o <IP address:port> Which SAGE2 server to connect to\n"
		"    -c <channels>        Audio Channels (2, 8 or 16 - default is 2)\n"
		"    -s <depth>           Audio Sample Depth (16 or 32 - default is 32)\n"
		"    -n <frames>          Number of frames to capture (default is unlimited)\n"
		"    -3                   Capture Stereoscopic 3D (Requires 3D Hardware support)\n"
		"    -u <SAM IP>          stream using SAM (audio), 127.0.0.1 by default \n"
		"    -j                   physical inputs for SAM inputs (audio)\n"
		"    -v                   stream using SAGE (video)\n"
		"    -y                   apply deinterlacing filter\n"
		"\n"
		"Capture video and/or audio to a file. Raw video and/or audio can be viewed with mplayer eg:\n"
		"\n"
		"    Capture -m2 -n 50 -f video.raw -a audio.raw\n"
		"    mplayer video.raw -demuxer rawvideo -rawvideo pal:uyvy -audiofile audio.raw -audio-demuxer 20 -rawaudio rate=48000\n"
	);
	exit(status);
}



bool handle_sac_audio_callback(unsigned int numChannels, unsigned int nframes, float** out, void*obj)
{
	unsigned int alen = nframes  * g_audioChannels * (g_audioSampleDepth / 8);
	unsigned int datalen = nframes  * g_audioChannels * 4;

	for (unsigned int k = 0; k < nframes*numChannels; k++) {
		audio_temp[k] = 0;
	}
	for (unsigned int ch = 0; ch < 1; ch++)
		for (unsigned int n = 0; n < nframes; n++)
			out[ch][n] = 0.0f;

#if 1
	unsigned int got = nframes  * g_audioChannels * (g_audioSampleDepth / 8);
	int numsamples = got / (g_audioChannels * (g_audioSampleDepth / 8));
	if (got == 0) {
		fprintf(stderr, "#");
	}
	if ((got > 0) && (got != alen)) {
		fprintf(stderr, ".");
	}
	if (got == alen) {
	}

	float *ptr = *out;
	int value; // for 32bit capture
	float fvalue;
	for (int k = 0; k < numsamples; k++) {
		for (int ch = 0; ch < g_audioChannels; ch++) {
			value = audio_temp[g_audioChannels*k+ch];
			fvalue = value / (float)(RAND_MAX); // for 32bit sound data
			out[ch][k] = fvalue;
		}
	}


#else
    for (unsigned int ch = 0; ch < 2; ch++)
       for (unsigned int n = 0; n < nframes; n++)
            out[ch][n] = rand() / (float)RAND_MAX;
#endif

    return true;
}


void myQuit(int sig)
{
        boost::property_tree::ptree data;
	fprintf(stderr, "Caught signal [%d]\n", sig);
	data.put<std::string>("id",uniqueID + "|0");
	// Quit SAM
        wsio->emit("stopMediaBlockStream", data);
	fprintf(stderr, "Sent stopMediaBlockStream\n", sig);
        //exit(0);
}

char *connectionName(int64_t vport)
{
    char *name;
    switch (vport) {
	case bmdVideoConnectionSDI:
	name = strdup("SDI");
	break;
	case bmdVideoConnectionHDMI:
	name = strdup("HDMI");
	break;
	case bmdVideoConnectionComponent:
	name = strdup("Component");
	break;
	case bmdVideoConnectionComposite:
	name = strdup("Composite");
	break;
	case bmdVideoConnectionSVideo:
	name = strdup("SVideo");
	break;
	case bmdVideoConnectionOpticalSDI:
	name = strdup("OpticalSDI");
	break;
	default:
	name = strdup("UNKNOWN");
	break;
    }
}


// function prototypes
std::vector<std::string> split(std::string s, char delim);
void ws_open(WebSocketIO* ws);
void ws_initialize(WebSocketIO* ws, boost::property_tree::ptree data);
void ws_requestNextFrame(WebSocketIO* ws, boost::property_tree::ptree data);
void ws_stopMediaCapture(WebSocketIO* ws, boost::property_tree::ptree data);
void ws_setupDisplayConfiguration(WebSocketIO* ws, boost::property_tree::ptree data);
void ws_confirmUpdateRecvd(WebSocketIO* ws, boost::property_tree::ptree data);
void ws_frameNumber(WebSocketIO* ws, boost::property_tree::ptree data);
void onPaint(int browserIdx);

// globals
bool continuous_resize;
char szWorkingDir[500];


int main(int argc, char* argv[]) {
    getcwd(szWorkingDir, sizeof(szWorkingDir));

    //Decklinkcapture code

    signal(SIGABRT,myQuit);//If program aborts go to assigned function
    signal(SIGTERM,myQuit);//If program terminates go to assigned function
    signal(SIGINT, myQuit);//If program terminates go to assigned function

	IDeckLinkIterator		*deckLinkIterator = CreateDeckLinkIteratorInstance();
    IDeckLinkAttributes*                            deckLinkAttributes = NULL;
	DeckLinkCaptureDelegate 	*delegate;
	IDeckLinkDisplayMode		*displayMode;
	BMDVideoInputFlags		inputFlags = 0;
	BMDDisplayMode			selectedDisplayMode = bmdModeNTSC;
	BMDPixelFormat			pixelFormat = bmdFormat8BitYUV;
	int				displayModeCount = 0;
	int				exitStatus = 1;
	int				ch;
	bool 				foundDisplayMode = false;
	HRESULT				result;
	int				dnum = 0;
	IDeckLink         		*tempLink = NULL;
	int				found = 0;
	bool				supported = 0;
    int64_t                                         ports;
    int                                                     itemCount;
    int                                                     vinput = 0;
    int64_t                                                     vport = 0;
    IDeckLinkConfiguration       *deckLinkConfiguration = NULL;
    bool flickerremoval = true;
    bool pnotpsf = true;
	std::string ws_uri;
	std::string xs,ys;
	int x,y;
	std::vector<std::string> args;
    // Default IP address for SAM
	//samIP = strdup("127.0.0.1");

	pthread_mutex_init(&sleepMutex, NULL);
	pthread_cond_init(&sleepCond, NULL);

	// Parse command line options
	while ((ch = getopt(argc, argv, "?h3c:d:s:f:a:l:m:n:o:p:t:u::vi:w:jyxbkeg")) != -1)
	{
		switch (ch)
		{
			case 'i':
				vinput = atoi(optarg);
				break;
			case 'g':
				dontWait = true;
				break;
			case 'u':
				//useSAM  = 1;
				printf("Set useSAM to 1: [%s]", optarg);
				if (optarg)
					//samIP = strdup(optarg);
				break;
			case 'x':
				//useDEINT = 1;
				useYUV422 = true;
				printf("useYUV422\n");
				break;
			case 'k':
				//useDEINT = 1;
				useBlocks = true;
				printf("useBlocks\n");
				break;
			case 'b':
				printf("benchmark timing/performance\n");
				benchmark = true;
				break;
			case 'e':
				printf("debug\n");
				debug = true;
				break;
			case 'v':
				useSAGE = 1;
				break;
			case 'j':
				physicalInputs = 1;
				break;
			case 'l':
				printf("location on sage2 desktop... to be implemented %s\n",optarg);
				args = split(optarg,',');
				sagePlacement = true;
				sageX = args[0];
				sageY = args[1];
				sageSX = args[2];
				sageSY = args[3];
				printf("x %s\n", sageX.c_str());
				printf("y %s\n", sageY.c_str());
				printf("sizex %s\n", sageSX.c_str());
				printf("sizey %s\n", sageSY.c_str());
				break;
			case 'd':
				card = atoi(optarg);
				break;
			case 'm':
				g_videoModeIndex = atoi(optarg);
				break;
			case 'c':
				g_audioChannels = atoi(optarg);
				if (g_audioChannels != 2 &&
				    g_audioChannels != 8 &&
					g_audioChannels != 16)
				{
					fprintf(stderr, "Invalid argument: Audio Channels must be either 2, 8 or 16\n");
					goto bail;
				}
				break;
			case 's':
				g_audioSampleDepth = atoi(optarg);
				if (g_audioSampleDepth != 16 && g_audioSampleDepth != 32)
				{
					fprintf(stderr, "Invalid argument: Audio Sample Depth must be either 16 bits or 32 bits\n");
					goto bail;
				}
				break;
			case 'f':
				g_videoOutputFile = optarg;
				break;
			case 'a':
				g_audioOutputFile = optarg;
				break;
			case 'n':
				g_maxFrames = atoi(optarg);
				break;
			case 'o':
				sage2Server = optarg;
				useSAGE = 1;
				break;
			case 'w':
				sage2title = optarg;
				useSAGE = 1;
				break;
			case '3':
				inputFlags |= bmdVideoInputDualStream3D;
				break;
			case 'p':
				switch(atoi(optarg))
				{
					case 0: pixelFormat = bmdFormat8BitYUV; break;
					case 1: pixelFormat = bmdFormat10BitYUV; break;
					case 2: pixelFormat = bmdFormat10BitRGB; break;
					default:
						fprintf(stderr, "Invalid argument: Pixel format %d is not valid", atoi(optarg));
						goto bail;
				}
				break;
			case 't':
				if (!strcmp(optarg, "rp188"))
					g_timecodeFormat = bmdTimecodeRP188Any;
    			else if (!strcmp(optarg, "vitc"))
					g_timecodeFormat = bmdTimecodeVITC;
    			else if (!strcmp(optarg, "serial"))
					g_timecodeFormat = bmdTimecodeSerial;
				else
				{
					fprintf(stderr, "Invalid argument: Timecode format \"%s\" is invalid\n", optarg);
					goto bail;
				}
				break;
			case '?':
			case 'h':
				usage(0);
		}
	}


    //DECKLINK CODE AFTER MAIN

	if (useSAGE && (pixelFormat != bmdFormat8BitYUV)) {
		fprintf(stderr, "SAGE works only with pixelFormat 1 (bmdFormat8BitYUV)");
		pixelFormat = bmdFormat8BitYUV;
	}

	if (!deckLinkIterator)
	{
		fprintf(stderr, "This application requires the DeckLink drivers installed.\n");
		goto bail;
	}

	/* Connect to the first DeckLink instance */
	while (deckLinkIterator->Next(&tempLink) == S_OK)
	{
		if (card != dnum) {
			dnum++;
			// Release the IDeckLink instance when we've finished with it to prevent leaks
			tempLink->Release();
			continue;
		}
		else {
			deckLink = tempLink;
			found = 1;
		}
		dnum++;
	}

	if (! found ) {
		fprintf(stderr, "No DeckLink PCI cards found.\n");
		goto bail;
	}
	if (deckLink->QueryInterface(IID_IDeckLinkInput, (void**)&deckLinkInput) != S_OK)
		goto bail;

	{
        // Query the DeckLink for its attributes interface
        result = deckLink->QueryInterface(IID_IDeckLinkAttributes, (void**)&deckLinkAttributes);
        if (result != S_OK)
	  {
	    fprintf(stderr, "Could not obtain the IDeckLinkAttributes interface - result = %08x\n", result);
	  }
	}

        result = deckLinkAttributes->GetFlag(BMDDeckLinkSupportsInputFormatDetection, &supported);
	if (result == S_OK)
		{
				fprintf(stderr, " %-40s %s\n", "Input mode detection supported ?", (supported == true) ? "Yes" : "No");
		}
	else
	{
			fprintf(stderr, "Could not query the input mode detection attribute- result = %08x\n", result);
	}

        fprintf(stderr, "Supported video input connections (-i [input #]:\n  ");
        itemCount = 0;
        result = deckLinkAttributes->GetInt(BMDDeckLinkVideoInputConnections, &ports);
        if (result == S_OK)
        {
                if (ports & bmdVideoConnectionSDI)
                {
                        fprintf(stderr, "%d: SDI, ", bmdVideoConnectionSDI);
                        itemCount++;
                }

                if (ports & bmdVideoConnectionHDMI)
                {
                        fprintf(stderr, "%d: HDMI, ", bmdVideoConnectionHDMI);
                        itemCount++;
                }

                if (ports & bmdVideoConnectionOpticalSDI)
                {
                        fprintf(stderr, "%d: Optical SDI, ", bmdVideoConnectionOpticalSDI);
                        itemCount++;
                }

                if (ports & bmdVideoConnectionComponent)
                {
                        fprintf(stderr, "%d: Component, ", bmdVideoConnectionComponent);
                        itemCount++;
                }

                if (ports & bmdVideoConnectionSVideo)
                {
                        fprintf(stderr, "%d: S-Video, ", bmdVideoConnectionSVideo);
                        itemCount++;
                }
        }
	fprintf(stderr, "\n");


	delegate = new DeckLinkCaptureDelegate();
	deckLinkInput->SetCallback(delegate);

	// Obtain an IDeckLinkDisplayModeIterator to enumerate the display modes supported on output
	result = deckLinkInput->GetDisplayModeIterator(&displayModeIterator);
	if (result != S_OK)

	{
		fprintf(stderr, "Could not obtain the video output display mode iterator - result = %08x\n", result);
		goto bail;
	}


	if (g_videoModeIndex < 0)
	{
		fprintf(stderr, "No video mode specified\n");
		usage(0);
	}

	if (g_videoOutputFile != NULL)
	{
		videoOutputFile = open(g_videoOutputFile, O_WRONLY|O_CREAT|O_TRUNC, 0664);
		if (videoOutputFile < 0)
		{
			fprintf(stderr, "Could not open video output file \"%s\"\n", g_videoOutputFile);
			goto bail;
		}
	}
	if (g_audioOutputFile != NULL)
	{
		audioOutputFile = open(g_audioOutputFile, O_WRONLY|O_CREAT|O_TRUNC, 0664);
		if (audioOutputFile < 0)
		{
			fprintf(stderr, "Could not open audio output file \"%s\"\n", g_audioOutputFile);
			goto bail;
		}
	}

	while (displayModeIterator->Next(&displayMode) == S_OK)
	{
		if (g_videoModeIndex == displayModeCount)
		{
			BMDDisplayModeSupport result;
			const char *displayModeName;

			foundDisplayMode = true;
			displayMode->GetName(&displayModeName);
			selectedDisplayMode = displayMode->GetDisplayMode();

			deckLinkInput->DoesSupportVideoMode(selectedDisplayMode, pixelFormat, bmdVideoInputFlagDefault, &result, NULL);
			if (result == bmdDisplayModeNotSupported)
			{
				fprintf(stderr, "The display mode %s is not supported with the selected pixel format\n", displayModeName);
				goto bail;
			}

			if (inputFlags & bmdVideoInputDualStream3D)
			{
				if (!(displayMode->GetFlags() & bmdDisplayModeSupports3D))
				{
					fprintf(stderr, "The display mode %s is not supported with 3D\n", displayModeName);
					goto bail;
				}
			}
			fprintf(stderr, "Selecting mode: %s\n", displayModeName);

			break;
		}
		displayModeCount++;
		displayMode->Release();
	}

	if (!foundDisplayMode)
	{
		fprintf(stderr, "Invalid mode %d specified\n", g_videoModeIndex);
		goto bail;
	}

    {
    // Query the DeckLink for its configuration interface
    result = deckLinkInput->QueryInterface(IID_IDeckLinkConfiguration, (void**)&deckLinkConfiguration);
    if (result != S_OK)
      {
	fprintf(stderr, "Could not obtain the IDeckLinkConfiguration interface: %08x\n", result);
      }
    }

    BMDVideoConnection conn;
    switch (vinput) {
    case 0:
      conn = bmdVideoConnectionSDI;
      break;
    case 1:
      conn = bmdVideoConnectionHDMI;
      break;
    case 2:
      conn = bmdVideoConnectionComponent;
      break;
    case 3:
      conn = bmdVideoConnectionComposite;
      break;
    case 4:
      conn = bmdVideoConnectionSVideo;
      break;
    case 5:
      conn = bmdVideoConnectionOpticalSDI;
      break;
    default:
      break;
    }
   conn = vinput;
    // Set the input desired
    result = deckLinkConfiguration->SetInt(bmdDeckLinkConfigVideoInputConnection, conn);
    if(result != S_OK) {
	fprintf(stderr, "Cannot set the input to [%d]\n", conn);
	goto bail;
    }

    // check input
    result = deckLinkConfiguration->GetInt(bmdDeckLinkConfigVideoInputConnection, &vport);
    if (vport == bmdVideoConnectionSDI)
	      fprintf(stderr, "Before Input configured for SDI\n");
    if (vport == bmdVideoConnectionHDMI)
	      fprintf(stderr, "Before Input configured for HDMI\n");

    if (deckLinkConfiguration->SetFlag(bmdDeckLinkConfigFieldFlickerRemoval, flickerremoval) == S_OK) {
      fprintf(stderr, "Flicker removal set : %d\n", flickerremoval);
    }
    else {
      fprintf(stderr, "Flicker removal NOT set\n");
    }

    if (deckLinkConfiguration->SetFlag(bmdDeckLinkConfigUse1080pNotPsF, pnotpsf) == S_OK) {
      fprintf(stderr, "bmdDeckLinkConfigUse1080pNotPsF: %d\n", pnotpsf);
    }
    else {
      fprintf(stderr, "bmdDeckLinkConfigUse1080pNotPsF NOT set\n");
    }

    result = deckLinkConfiguration->GetInt(bmdDeckLinkConfigVideoInputConnection, &vport);
    if (vport == bmdVideoConnectionSDI)

	      fprintf(stderr, "After Input configured for SDI\n");
    if (vport == bmdVideoConnectionHDMI)
	      fprintf(stderr, "After Input configured for HDMI\n");


    result = deckLinkInput->EnableVideoInput(selectedDisplayMode, pixelFormat, inputFlags);
    if(result != S_OK)
    {
		fprintf(stderr, "Failed to enable video input. Is another application using the card?\n");
        goto bail;
    }


    result = deckLinkInput->EnableAudioInput(bmdAudioSampleRate48kHz, g_audioSampleDepth, g_audioChannels);
    if(result != S_OK)
    {
      fprintf(stderr, "Failed to enable audio input with 48kHz, depth %d, %d channels\n",
              g_audioSampleDepth, g_audioChannels);
        goto bail;

    }

	if (useSAGE) {
		// Get capture size
		sageW = displayMode->GetWidth();
		sageH = displayMode->GetHeight();
		// Compute the actual framerate
		displayMode->GetFrameRate(&frameRateDuration, &frameRateScale);
		sageFPS = (double)frameRateScale / (double)frameRateDuration;
		fprintf(stderr, "GetFrameRate: %10ld %10ld --> fps %g\n", (long)frameRateScale, (long)frameRateDuration, sageFPS);

		// SAGE setup
		fprintf(stderr, "SAIL configuration was initialized by decklinkcapture.conf\n");
		fprintf(stderr, "\twidth %d height %d fps %g\n", sageW, sageH, sageFPS);

		if (pixelFormat != bmdFormat8BitYUV)
			fprintf(stderr, "SAGE works only with pixelFormat 1 (bmdFormat8BitYUV)\n");

		printf("SAIL initialized");


    avpicture_alloc(&picture_curr, AV_PIX_FMT_UYVY422, sageW, sageH);

    //usedeint code AM
#if defined(DEINTERLACING)
		//////////////////////////////////
		// FFMPEG for de-interlacing
		//////////////////////////////////
		if (useDEINT) {
			picture_prev = (AVPicture*) alloc_picture(SAGE_YUV, sageW, sageH);
			picture_next = (AVPicture*) alloc_picture(PIX_FMT_YUV422P, sageW, sageH);

			img_convert_toplanar = sws_getContext(sageW, sageH, SAGE_YUV,
				sageW, sageH, PIX_FMT_YUV422P, SWS_BICUBIC, NULL, NULL, NULL);
			if (img_convert_toplanar == NULL) {
				sage::printLog("Cannot img_convert_toplanar converter");
				exit(1);
			}
			img_convert_topacked = sws_getContext(sageW, sageH, PIX_FMT_YUV422P,
				sageW, sageH, SAGE_YUV, SWS_BICUBIC, NULL, NULL, NULL);
			if (img_convert_topacked == NULL) {
				sage::printLog("Cannot img_convert_topacked converter");
				exit(1);
			}
		}
		//////////////////////////////////
#endif
        avpicture_alloc(&picture_yuv420p, AV_PIX_FMT_YUV420P, sageW, sageH);
        img_convert_toyuv420p = sws_getContext(sageW, sageH, AV_PIX_FMT_UYVY422, sageW, sageH, AV_PIX_FMT_YUV420P, 0, 0, 0, 0);

        //////// NO SAM Support now, so remove!
#if 0
		//////////////////////////////////
		#define BIG_AUDIO_BUFFER (10*1024*1024)
		char *audio_buffer;
		int buffer_length = BIG_AUDIO_BUFFER * g_audioChannels * (g_audioSampleDepth / 8);
		audio_buffer = (char*)malloc(buffer_length);
		memset(audio_buffer, 0, buffer_length);
		//audio_ring = new Ring_Buffer(audio_buffer, buffer_length);
		fprintf(stderr, "Audio ring created\n");
		/////////////////////////////////

        //////////////////////////////////
		// SAM
		//////////////////////////////////
		if (useSAM) {
			char *displayModeString = NULL;
			char *inputString = NULL;
			const char *modelName = NULL;
			// Sadly, jack uses short names
			char samName[32];
			memset(samName, 0, 32);
			displayMode->GetName((const char **) &displayModeString);
			deckLink->GetModelName(&modelName);
			inputString = connectionName(vport);
			snprintf(samName, 32, "[%s-%s] [%d-%d]", inputString, displayModeString, g_audioChannels, g_audioSampleDepth);
			free(displayModeString);

			printf("Using SAM IP: %s", samIP);

			//m_sac = new sam::StreamingAudioClient();
			//m_sac->init(g_audioChannels, sam::TYPE_BASIC, samName, samIP, 7770);
			//int returnCode = m_sac->start(0, 0, sageW, sageH, 0);

			if (physicalInputs)
			{
				unsigned int* inputs = new unsigned int[g_audioChannels];
				for (unsigned int n = 0; n < g_audioChannels; n++)
				{
					inputs[n] = n + 1;
				}
				printf("Calling SetPhysicalInputs");
				//m_sac->setPhysicalInputs(inputs);
				delete[] inputs;
			}
			/*else {
				// register SAC callback
				m_sac->setAudioCallback(handle_sac_audio_callback, NULL);
			}*/

		}
#endif
		//////////////////////////////////
	pthread_t thId;

    }

    result = deckLinkInput->StartStreams();
    if(result != S_OK)
    {
        goto bail;
    }
	// All Okay.
	exitStatus = 0;

    // Create the websocket for SAGE2
    ws_uri = "ws://" + std::string(sage2Server);
    wsio = new WebSocketIO(ws_uri);
    wsio->open(ws_open);

	// Block main thread until signal occurs
	pthread_mutex_lock(&sleepMutex);
	pthread_cond_wait(&sleepCond, &sleepMutex);
	pthread_mutex_unlock(&sleepMutex);
	fprintf(stderr, "Stopping Capture\n");
bail:

	if (videoOutputFile)
		close(videoOutputFile);
	if (audioOutputFile)
		close(audioOutputFile);
	if (displayModeIterator != NULL)
	{
		displayModeIterator->Release();
		displayModeIterator = NULL;
	}
    if (deckLinkInput != NULL)
    {
        deckLinkInput->Release();
        deckLinkInput = NULL;
    }
    if (deckLink != NULL)
    {
        deckLink->Release();
        deckLink = NULL;
    }
	if (deckLinkIterator != NULL)
		deckLinkIterator->Release();

    //if (m_sac) delete m_sac;
    //if (sageInf) deleteSAIL(sageInf);
    return exitStatus;
}






/******** Helper Functions ********/
std::vector<std::string> split(std::string s, char delim) {
    std::stringstream ss(s);
    std::string item;
    std::vector<std::string> elems;
    while (std::getline(ss, item, delim)) {
        elems.push_back(item);
    }
    return elems;
}

/******** WebSocket Callback Functions ********/
void ws_open(WebSocketIO* ws) {
    printf("WEBSOCKET OPEN\n");


    ws->on("initialize", ws_initialize);
    ws->on("requestNextFrame", ws_requestNextFrame);
    ws->on("stopMediaCapture", ws_stopMediaCapture);
    ws->on("setupDisplayConfiguration", ws_setupDisplayConfiguration);
    // for debugging only!
    ws->on("confirmUpdateReceived", ws_confirmUpdateRecvd);
    ws->on("frameNumber", ws_frameNumber);

    // send addClient message
    boost::property_tree::ptree data;
    data.put<std::string>("clientType", "deckLink");
    boost::property_tree::ptree req;
    req.put<bool>("config",  true);
    req.put<bool>("version", false);
    req.put<bool>("time",    false);
    req.put<bool>("console", false);
    data.put_child("requests", req);
    //data.put<bool>("sendsMediaStreamFrames", true);
    ws->emit("addClient", data);
    fprintf(stderr, "Sent addClient\n");
}

void ws_setupDisplayConfiguration(WebSocketIO* ws, boost::property_tree::ptree data) {
                    //std::string wb_url = data.get<std::string> ("url");
                    fprintf(stderr, "\nSending startNewMediaBlockStream\n");
                    boost::property_tree::ptree emit_data;
                    emit_data.put<std::string>("id",uniqueID + "|0");
                    emit_data.put<std::string>("title", sage2title);
                    emit_data.put<int>("width", sageW);
                    emit_data.put<int>("height", sageH);
		    if (sagePlacement) {
                      emit_data.put<std::string>("x", sageX);
                      emit_data.put<std::string>("y", sageY);
                      emit_data.put<std::string>("dwidth", sageSX);
                      emit_data.put<std::string>("dheight", sageSY);
                    }
		    if (useYUV422) {
                      emit_data.put<string>("colorspace", "YUV422");
		    } else {
                      emit_data.put<string>("colorspace", "YUV420p");
		    }
                    wsio->emit("startNewMediaBlockStream", emit_data);
                    //sendFrame();
}

void ws_initialize(WebSocketIO* ws, boost::property_tree::ptree data) {
    uniqueID = data.get<std::string> ("UID");
    fprintf(stderr, "SAGE2 ID> %s\n", uniqueID.c_str());
}

static long int sage2frames = 0;
static long int serverFrameNumber = 0;
static long int videoframes = 0;

std::string emit_data = "";

void serializeFrame() {
    // prepare data for send in emit_data
    emit_data = uniqueID;
    emit_data.append("|0");
    emit_data.push_back('\0');
#if defined(DEINTERLACING)
    emit_data.append(std::string((uint8_t*)picture_prev->data[0]));
#else
    int len = 0;
    std::string s1;
    if (useYUV422) {
      //printf("Sendframe useYUV422\n");
      // for YUV422, 2 bytes per pixel (four byte macro pixel covers two regular pixels)
      len = sageW * sageH * 2;
      std::string s2(picture_curr.data[0], picture_curr.data[0] + len);
      s1 = s2;
    } else {
      len = sageW * sageH * 1.5;
      //printf("sending %d bytes\n",len);
      std::string s3(picture_yuv420p.data[0], picture_yuv420p.data[0] + len);
      s1 = s3;
    }
    emit_data.append(s1);
#endif
    // data ready for send
}

void sendYuv420FrameAsPixelBlocks() {
        if (debug && benchmark) printf("sendYuv420FrameAsPixelBlocks %f\n", getTime());
        int width = sageW;
        int height = sageH;
        // send FrameInit
        std::string emit_data = uniqueID;
        emit_data.append("|0");
        emit_data.push_back('\0');
        wsio->emit_binary("updateMediaBlockStreamFrameInit", (unsigned char*)emit_data.c_str(), emit_data.length());
        // send FrameBlocks
        int horizontalBlocks = ceil(width*1.0/maxSize);
        int verticalBlocks   = ceil(height*1.0/maxSize);
        //if (debug) printf("pixblocks %d,%d\n", horizontalBlocks, verticalBlocks);
        int uStart = width*height;
        int vStart = uStart + (width*height/4);
        for (int i=0; i<verticalBlocks; i++) {
                for (int j=0; j<horizontalBlocks; j++) {
                        //if (debug) printf("sendYuv420FrameAsPixelBlocks %d %d\n", i, j);
                        int bWidth  = (j+1)*maxSize > width  ? width -(j*maxSize) : maxSize;
                        int bHeight = (i+1)*maxSize > height ? height-(i*maxSize) : maxSize;
                        //int buStart = bWidth*bHeight;
                        //int bvStart = buStart + (bWidth*bHeight/4);

                        // header
                        emit_data = uniqueID;
                        emit_data.append("|0");
                        emit_data.push_back('\0');
                        std::string ydata = "";
                        std::string budata = "";
                        std::string bvdata = "";
                        for (int k=0; k<bHeight; k++) {
                                //if (debug) printf("sendYuv420FrameAsPixelBlocks %d %d %d\n", i, j, k);
                                int row = i*maxSize + k;
                                int col = j*maxSize;
                                int yStart = row*width + col;

                                //yuvBuffer.copy(block, k*bWidth, yStart, yStart+bWidth);
				uint8_t* startPtr = picture_yuv420p.data[0] + yStart;
				std::string s1(startPtr, startPtr + bWidth);
				ydata.append(s1);
                                if(k%2 == 0) {
                                        int uvRow   = floor(row*1.0/2);
                                        int uvCol   = floor(col*1.0/2);
                                        int uvStart = uvRow*width/2 + uvCol;

                                        //yuvBuffer.copy(block, buStart+k/2*bWidth/2, uStart+uvStart, uStart+uvStart+bWidth/2);
                                	startPtr = picture_yuv420p.data[0] + uStart+uvStart;
                                	std::string s2(startPtr, startPtr + bWidth/2);
                                	budata.append(s2);
                                        //yuvBuffer.copy(block, bvStart+k/2*bWidth/2, vStart+uvStart, vStart+uvStart+bWidth/2);
                                	startPtr = picture_yuv420p.data[0] + vStart+uvStart;
                                	std::string s3(startPtr, startPtr + bWidth/2);
                                	bvdata.append(s3);
                                }
                        }
			// send frame block
                        emit_data.append(ydata);
                        emit_data.append(budata);
                        emit_data.append(bvdata);
                        //if (debug) printf("sendYuv420FrameAsPixelBlocks updateMediaBlockStreamFrameBlock\n");
                        wsio->emit_binary("updateMediaBlockStreamFrameBlock", (unsigned char*)emit_data.c_str(), emit_data.length());
                }
        }
        // send completion frame
        emit_data = uniqueID;
        emit_data.append("|0");
        emit_data.push_back('\0');
        wsio->emit_binary("updateMediaBlockStreamFrameFinal", (unsigned char*)emit_data.c_str(), emit_data.length());
        if (debug && benchmark) printf("sendYuv420FrameAsPixelBlocks done %f\n", getTime());
};

char blockData[maxSize*maxSize*2 + 64];

// send frame as pixel blocks
void sendYuv422FrameAsPixelBlocksOld() {
        if (debug && benchmark) printf("sendYuv422FrameAsPixelBlocks %f\n", getTime());
        int width = sageW;
        int height = sageH;
        // keep synced with sage2server
        std::string emit_data;
        emit_data = uniqueID;
        emit_data.append("|0");
        emit_data.push_back('\0');
        wsio->emit_binary("updateMediaBlockStreamFrameInit", (unsigned char*)emit_data.c_str(), emit_data.length());
        int horizontalBlocks = ceil(width*1.0/maxSize);
        int verticalBlocks   = ceil(height*1.0/maxSize);
        //printf("pixblocks %d,%d\n", horizontalBlocks, verticalBlocks);
        strcpy(blockData,emit_data.c_str());
        int hdrlen = emit_data.length();
        for (int i=0; i<verticalBlocks; i++) {
                for (int j=0; j<horizontalBlocks; j++) {
                        // header
                        // block dimensions
                        int bWidth  = (j+1)*maxSize > width  ? width -(j*maxSize) : maxSize;
                        int bHeight = (i+1)*maxSize > height ? height-(i*maxSize) : maxSize;
                        // current block corner
                        // - i*maxSize pixel rows down, each width pixels wide
                        // - j*maxSize pixels across
                        //int corner = 2*i*maxSize*width + 2*j*maxSize;
                        // assemble: for all row indices in block
                        char* bdPtr = blockData + hdrlen;
                        for (int k=0; k<bHeight; k++) {
                                // pixel row, col
                                int row = i*maxSize + k;
                                int col = j*maxSize;
                                int start = 2*(row*width + col);
                                uint8_t* startPtr = picture_curr.data[0] + start;
                                strncpy(bdPtr,(char*)startPtr,bWidth*2);
				bdPtr += bWidth*2;
                        }
                        // send block
                        wsio->emit_binary("updateMediaBlockStreamFrameBlock", (unsigned char*)blockData, bWidth*bHeight*2 + hdrlen);
                }
        }
        if (debug && benchmark) printf("sendYuv422FrameAsPixelBlocks init done %f\n", getTime());
}

void sendYuv422FrameAsPixelBlocks() {
	sendYuv422FrameAsPixelBlocksOld();
}

void sendYuv422FrameAsPixelBlocksFinal() {
        // send completion frame
        string emit_data = uniqueID;
        emit_data.append("|0");
        emit_data.push_back('\0');
        wsio->emit_binary("updateMediaBlockStreamFrameFinal", (unsigned char*)emit_data.c_str(), emit_data.length());
        if (debug && benchmark) printf("sendYuv422FrameAsPixelBlocks done %f\n", getTime());
};

double frameRate = 30;

bool dropFrame() {
    // how many frames behind (assume half measured given RTT is double latency)
    int lagframes = (sage2frames - serverFrameNumber)/2;
    // how many ms behind (could also calculate more directly with timestamp in frameNumber packet!)
    int lagms = lagframes * 1000 / frameRate;
    return !(
        lagms < 30
        || ((lagms < 60) && (videoframes%2==0))
        || ((lagms < 120) && (videoframes%3==0))
        || ((lagms < 500) && (videoframes%5==0))
        || (videoframes%10==0)
    );
}

void sendFrameStandard() {
      serializeFrame();
      wsio->emit_binary("updateMediaBlockStreamFrame", (unsigned char*)emit_data.c_str(), emit_data.length());
      boost::property_tree::ptree data;
      data.put<std::string>("id",uniqueID + "|0");
      data.put<int>("frameNumber", sage2frames);
      //wsio->emit("frameNumber", data);
}


void showFrameRate() {
    double post = getTime();
    if (benchmark) {
        if (sage2frames <= 5) {
          printf("SAGE2: frame %d\n",sage2frames);
        }
        if (videoframes % 30 == 0) {
          double now = getTime();
          frameRate = (1.0 * sage2frames) / now;
          printf("SAGE2: frame rate %f - sent %d frames in %fs (remote lag %d frames)\n", frameRate, sage2frames, now, sage2frames-serverFrameNumber);
        }
    }
}

//bool readyForBlocks = true;
// sentBlocks == !readyForBlocks

void sendFrame() {
  //if (debug && benchmark) printf("sendFrame\n");
  got_request = false;

  videoframes++;
  //if (dropFrame()) {
  //  printf("dropFrame\n");
  //  return;
  //}
  sage2frames++;
  if (!useBlocks) {
    sendFrameStandard();
  } else if (useYUV422) {
    //if (readyForBlocks) {
      sendYuv422FrameAsPixelBlocks();
    //  readyForBlocks = false;
    //}
    //if (!readyForBlocks && got_request) {
      sendYuv422FrameAsPixelBlocksFinal();
      //readyForBlocks = true;
      //got_request = false;
    //}
  } else {
    sendYuv420FrameAsPixelBlocks();
  }
  showFrameRate();
}

void ws_requestNextFrame(WebSocketIO* ws, boost::property_tree::ptree data) {
	if (debug && benchmark) {
          printf("SAGE2: requestNextFrame: %fs\n", getTime()); 
	}
	got_request = true;
        //sendFrame();
}

void ws_confirmUpdateRecvd(WebSocketIO* ws, boost::property_tree::ptree data) {
    if (benchmark || debug) printf("ws_confirmUpdateRecvd %f\n", getTime()); 
}

void ws_frameNumber(WebSocketIO* ws, boost::property_tree::ptree data) {
    serverFrameNumber = data.get<int>("frameNumber");
    //if (benchmark || debug) printf("ws_frameNumber server received frame %d\n", serverFrameNumber); 
}

void ws_stopMediaCapture(WebSocketIO* ws, boost::property_tree::ptree data) {
    std::string streamId = data.get<std::string> ("streamId");
    int idx = atoi(streamId.c_str());
    printf("STOP MEDIA CAPTURE: %d\n", idx);

    exit(0);
}

/******** Auxiliary App Functions ********/
std::string AppGetWorkingDirectory() {
  return szWorkingDir;
}

void AppQuitMessageLoop() {
}

void startTimer()
{
    gettimeofday(&tv_start, 0);
}

double getTime()
{
    struct timeval tv;

    gettimeofday(&tv, 0);
    return (double)(tv.tv_sec - tv_start.tv_sec) + (double)(tv.tv_usec - tv_start.tv_usec) / 1000000.0 ;
}
