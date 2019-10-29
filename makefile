all: run_cqp_detectotron

run_cqp_detectotron: cqp_detectotron.cpp mini_parser.cpp websocket_relay/websocket_relay.cpp websocket_relay/base64.cpp websocket_relay/sha1.cpp makefile ./UQDevices/CTimeTag/Include/*.h ./UQDevices/CTimeTag/Linux/libtimetag64.so
	gcc  -m64 -O3 -g -rdynamic -std=c++11 \
	cqp_detectotron.cpp \
	mini_parser.cpp \
	websocket_relay/websocket_relay.cpp \
	websocket_relay/base64.cpp \
	websocket_relay/sha1.cpp \
	-orun_cqp_detectotron -lpthread -lstdc++ \
	-Iwebsocket_relay -ICTimeTagLib \
	-L./UQDevices/CTimeTag/Linux -lusb-1.0 -ltimetag64 

start64:run_cqp_detectotron
	sudo ./run_cqp_detectotron

clean:
	rm -f run_cqp_detectotron
