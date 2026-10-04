CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2

ifeq ($(OS),Windows_NT)
    LDFLAGS = -lws2_32
    EXT = .exe
else
    LDFLAGS = -pthread
    EXT =
endif

all: server$(EXT) client$(EXT)

server$(EXT): server.cpp protocol.h crc8.h
	$(CXX) $(CXXFLAGS) -o server$(EXT) server.cpp $(LDFLAGS)

client$(EXT): client.cpp protocol.h crc8.h
	$(CXX) $(CXXFLAGS) -o client$(EXT) client.cpp $(LDFLAGS)

clean:
ifeq ($(OS),Windows_NT)
	del /f /q *.exe *.o 2>nul || exit 0
else
	rm -f server client *.o
endif
