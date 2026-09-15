CXX ?= g++
CXXFLAGS ?= -std=c++26 -freflection -Wall -Wextra -O0 -g
CPPFLAGS += -Iinclude -Isrc

.PHONY: all clean demo

all: zb_rpc_server zb_rpc_client

zb_rpc_server: src/server.cpp src/sim_zigbee.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $^

zb_rpc_client: src/client.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $@ $^

demo: all
	./zb_rpc_server & echo $$! > .server.pid
	sleep 0.2
	./zb_rpc_client; status=$$?; \
	kill $$(cat .server.pid) 2>/dev/null; rm -f .server.pid; exit $$status

clean:
	rm -f zb_rpc_server zb_rpc_client .server.pid
