CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -I../include

test_core: test_core.cpp ../source/save.cpp ../source/md5.cpp
	$(CXX) $(CXXFLAGS) -o $@ $^
	./$@

clean:
	rm -f test_core

.PHONY: clean
