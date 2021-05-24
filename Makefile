CXXFLAGS= -std=c++17
LDLIBS= -lpistache -lcrypto -lssl -lpthread -lmosquitto

include easymake.mk

# greenhouse: greenhouse_app.cpp
#	g++ $< -o $@ -lpistache -lcrypto -lssl -lpthread
