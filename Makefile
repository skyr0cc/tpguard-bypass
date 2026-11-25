CXX := x86_64-w64-mingw32-g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -mwindows -municode -O2
LDLIBS := -lws2_32 -luser32 -lgdi32 -lshell32 -lgdiplus

TARGET := build/skyro_wtf.exe
SOURCE := src/skyro_wtf.cpp

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCE)
	mkdir -p build/assets
	$(CXX) $(CXXFLAGS) $(SOURCE) -o $(TARGET) $(LDLIBS)
	cp -R assets/. build/assets/

clean:
	rm -rf build
