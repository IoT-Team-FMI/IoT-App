all: bin/greenhouse_app
bin/greenhouse_app: bin/greenhouse_app.o
	g++ $^  -o $@  -lpistache -lcrypto -lssl -lpthread
all: bin/microwave_example
bin/microwave_example: bin/microwave_example.o
	g++ $^  -o $@  -lpistache -lcrypto -lssl -lpthread
