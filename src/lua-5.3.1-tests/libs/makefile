# change this variable to point to the directory with Lua headers
# of the version being tested
LUA_DIR = ../..

CC = gcc
CXX = g++

# compilation should generate Dynamic-Link Libraries
CFLAGS = -Wall -std=gnu99 -O2 -I$(LUA_DIR) -fPIC -shared
CXXFLAGS = -Wall -std=c++98 -O2 -I$(LUA_DIR) -fPIC -shared

# libraries used by the tests
all: lib1.so lib11.so lib2.so lib21.so lib2-v2.so

lib1.so: lib1.cc
	$(CXX) $(CXXFLAGS) -o lib1.so lib1.cc

lib11.so: lib11.cc
	$(CXX) $(CXXFLAGS) -o lib11.so lib11.cc

lib2.so: lib2.cc
	$(CXX) $(CXXFLAGS) -o lib2.so lib2.cc

lib21.so: lib21.cc
	$(CXX) $(CXXFLAGS) -o lib21.so lib21.cc

lib2-v2.so: lib2.so
	mv lib2.so ./lib2-v2.so
