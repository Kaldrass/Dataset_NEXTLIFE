# Toolchain
CXX       := g++
CPPFLAGS  := -I include -I third_party
CXXFLAGS  := -std=gnu++17 -O3 -g -Wall -Wextra -pedantic
LDFLAGS   :=
LDLIBS    := -lcryptopp -lboost_iostreams

# Sources / Objects
SRC_APP   := $(wildcard src/*.cpp)
SRC_3P    := $(wildcard third_party_src/*.cpp)
SRC       := main.cpp $(SRC_APP) $(SRC_3P)
OBJ       := $(SRC:.cpp=.o)
EXE       := app

all: $(EXE)
	$(RM) -f $(OBJ)

$(EXE): $(OBJ)
	$(CXX) $(LDFLAGS) -o $@ $(OBJ) $(LDLIBS)
	@echo "$(EXE) prêt!"

%.o: %.cpp
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

clean:
	$(RM) -f $(OBJ) $(EXE)
