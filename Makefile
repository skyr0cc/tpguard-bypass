CXX := x86_64-w64-mingw32-g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -mwindows -municode -O2 -Ivendor/imgui
LDFLAGS := -static -static-libgcc -static-libstdc++
LDLIBS := -lws2_32 -luser32 -lgdi32 -lshell32 -lgdiplus -ld3d11 -ldxgi -ld3dcompiler -ldwmapi

TARGET := build/skyro_wtf.exe

IMGUI_SOURCES := \
	vendor/imgui/imgui.cpp \
	vendor/imgui/imgui_draw.cpp \
	vendor/imgui/imgui_tables.cpp \
	vendor/imgui/imgui_widgets.cpp \
	vendor/imgui/backends/imgui_impl_win32.cpp \
	vendor/imgui/backends/imgui_impl_dx11.cpp

SOURCES := src/skyro_wtf.cpp $(IMGUI_SOURCES)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	mkdir -p build/assets
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(SOURCES) -o $(TARGET) $(LDLIBS)
	cp -R assets/. build/assets/

clean:
	rm -rf build
