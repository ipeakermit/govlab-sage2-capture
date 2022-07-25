#include <iostream>



#include "websocketio.h"

void stringMessage(WebSocketIO* ws, boost::property_tree::ptree data) {
    std::ostringstream oss;
    boost::property_tree::write_json(oss, data, false);
    fprintf(stderr, "stringMessage: %s\n", oss.str().c_str());

}
void stringMessage2(WebSocketIO* ws, boost::property_tree::ptree data) {
    std::ostringstream oss;
    boost::property_tree::write_json(oss, data, false);
    fprintf(stderr, "stringMessage2: %s\n", oss.str().c_str());

}

void binaryMessage(WebSocketIO* ws, unsigned char* data, long length) {
    int i;

    fprintf(stderr, "binary: ");
    for (i=0; i<length; i++) {
	fprintf(stderr, "%d ", data[i]);
    }
    fprintf(stderr, "\n");
}

void ws_open(WebSocketIO* ws) {
    printf("WEBSOCKET OPEN\n");

    ws->on("stringMessage",  stringMessage);
    ws->on("binaryMessage",  binaryMessage);

	boost::property_tree::ptree data;
	data.put<int>("x", 44);
	data.put<int>("y", 144);
	data.put<int>("w", 244);
	data.put<int>("h", 344);
	ws->emit("requestStringMessage", data);

	int i;
	unsigned char *buf = new unsigned char[10];
	for (i=0; i<10; i++) buf[i] = i;
	ws->emit_binary("requestBinaryMessage", buf, 10);
}

int
main(int argc, char **argv)
{
	std::string address = "ws://localhost:8000";
	if (argc > 1) address = argv[1];

	WebSocketIO* wsio = new WebSocketIO(address);
	wsio->open(ws_open);

	return 0;
}

