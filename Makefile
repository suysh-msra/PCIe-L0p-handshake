CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -O2 -Iinclude
SRCDIR   := src
SOURCES  := $(SRCDIR)/pcie_port.cpp \
            $(SRCDIR)/pcie_link.cpp \
            $(SRCDIR)/visualizer.cpp \
            $(SRCDIR)/scenarios.cpp \
            $(SRCDIR)/main.cpp
TARGET   := l0p_sim

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES)
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f $(TARGET)
