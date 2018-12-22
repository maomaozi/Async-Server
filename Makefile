CC = gcc
CXX = g++

CXX_FLAGS = -std=c++11 -O3


ALL_OBJ = async_event_processor.so


all: $(ALL_OBJ)


async_event_processor.so: async_event_processor.h
	$(CXX) $(CXX_FLAGS) -shared -fPIC async_event_processor.h -o async_event_processor.so
	
test_http: async_event_processor.so
	#$(CXX) $(CXX_FLAGS) test_http.cpp async_event_processor.so -o test_http
	$(CXX) $(CXX_FLAGS) test_http.cpp async_event_processor.h -o test_http -lpthread
	
echo_http: async_event_processor.so
	$(CXX) $(CXX_FLAGS) echo_http.cpp async_event_processor.h -o echo_http -lpthread

.PHONY: clean

clean:
	rm -f *.so test_http echo_http
