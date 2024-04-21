CXX=clang++
CXXFLAGS=-std=c++17 -g -Wall -I ../
LDFLAGS=-stdlib=libc++

xmlfs: xmlfs.o ../Quizno50XML/FileString.o ../Quizno50XML/Quizno50XML.o
	$(CXX) -o $@ -I ../ $^ -lfuse3

