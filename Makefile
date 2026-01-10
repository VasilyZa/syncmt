CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O3 -march=native -mtune=native -flto
LDFLAGS = -luring -lpthread

SRCDIR = src
INCDIR = include
OBJDIR = obj
BINDIR = bin

TARGET = $(BINDIR)/syncmt

SOURCES = $(wildcard $(SRCDIR)/*.cpp)
OBJECTS = $(patsubst $(SRCDIR)/%.cpp,$(OBJDIR)/%.o,$(SOURCES))

.PHONY: all clean debug release

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BINDIR)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJDIR)/%.o: $(SRCDIR)/%.cpp | $(OBJDIR)
	$(CXX) $(CXXFLAGS) -I$(INCDIR) -c $< -o $@

$(OBJDIR):
	@mkdir -p $(OBJDIR)

$(BINDIR):
	@mkdir -p $(BINDIR)

debug: CXXFLAGS += -g -DDEBUG -fsanitize=address -fsanitize=leak
debug: clean $(TARGET)

release: CXXFLAGS += -DNDEBUG
release: clean $(TARGET)

clean:
	@rm -rf $(OBJDIR) $(BINDIR)

install: $(TARGET)
	@install -m 755 $(TARGET) /usr/local/bin/

uninstall:
	@rm -f /usr/local/bin/syncmt

help:
	@echo "Available targets:"
	@echo "  all      - Build the project (default)"
	@echo "  debug    - Build with debug symbols and sanitizers"
	@echo "  release  - Build optimized release version"
	@echo "  clean    - Remove build artifacts"
	@echo "  install  - Install syncmt to /usr/local/bin"
	@echo "  uninstall- Remove syncmt from /usr/local/bin"
	@echo "  help     - Show this help message"
