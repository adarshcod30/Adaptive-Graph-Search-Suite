CXX = g++
CXXFLAGS = -std=c++17 -O3 -Wall -Wextra
SRC = $(wildcard src/*.cpp)
OBJ = $(SRC:.cpp=.o)
BIN = bin/adaptive_map

all: $(BIN)

$(BIN): $(OBJ)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $^

clean:
	rm -f src/*.o $(BIN)

run-example:
	./examples/run_example.sh

test:
	$(BIN) --test
