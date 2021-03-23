greenhouse: greenhouse_app.cpp
	g++ $< -o $@ -lpistache -lcrypto -lssl -lpthread
