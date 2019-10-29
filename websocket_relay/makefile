all: websocket_relay

websocket_relay: websocket_relay.cpp base64.cpp miniz.cpp sha1.cpp  makefile 
	gcc  -m64 websocket_relay.cpp base64.cpp miniz.cpp sha1.cpp -DSTANDALONE -owebsocket_relay -lpthread -lstdc++ -ICTimeTagLib -L../CTimeTag/Linux -lusb-1.0 -ltimetag64 

