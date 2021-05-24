# Smart Greenhouse

## Getting Started

Git clone this project in your machine.

### Prerequisites

Build tested on Ubuntu Server. Pistache doesn't support Windows, but you can use something like [WSL](https://docs.microsoft.com/en-us/windows/wsl/install-win10) or a virtual machine with Linux.

You will need to have a C++ compiler. I used g++ that came preinstalled. Check using `g++ -v`

You will need to install the [Pistache](https://github.com/pistacheio/pistache) library.
On Ubuntu, you can install a pre-built binary as described [here](http://pistache.io/docs/#installing-pistache).

[Nlohmann JSON](https://github.com/nlohmann/json)
```
sudo apt-get install nlohmann-json3-dev
```
G++, Cmake
```
sudo apt-install g++
sudo apt-install cmake
```
[Pistache](http://pistache.io/)
```
sudo add-apt-repository ppa:pistache+team/unstable
sudo apt update
sudo apt install libpistache-dev
```
[Mosquitto](https://mosquitto.org/)
```
sudo apt-add-repository ppa:mosquitto-dev/mosquitto-ppa
sudo apt-get update
sudo apt install mosquitto mosquitto-clients libmosquitto-dev
```

### Building

#### Using Make

You can build the `greenhouse` executable by running `make`.

### Running

To start the server run\
`./bin/greenhouse_app`

Your server should display the number of cores being used and no errors.

To test, open up another terminal, and type\
`curl http://localhost:9080/ready`

Number 1 should display.

Now you have the server running
## Exemple request

Vizualizare temperatura curenta
```
curl -XGET http://127.0.0.1:9080/settings/temperature
```

Setare temperatura la 25
```
curl -XPOST http://127.0.0.1:9080/settings/temperature/25
```  

Adauga o preconfigurare pentru un tip de planta
```
curl --header "Content-Type: application/json" \
  --request POST \
  --data '{
    "luminosity":11,
    "humidity": 12,
    "temperature":13,
    "carbonDioxide":22,
    "plantType": "salata"
    }' \
  http://127.0.0.1:9080/preconfigurations
```  

Vizualizare ora si data pentru irigare
```
curl -XGET http://127.0.0.1:9080/irigationTime
```


## Built With

* [Pistache](https://github.com/pistacheio/pistache) - Web server
* [Mosquitto](https://github.com/eclipse/mosquitto) - MQTT broker
* [Nlohmann JSON](https://github.com/nlohmann/json) - Serialization


## License

This project is licensed under the Apache 2.0 Open Source Licence - see the [LICENSE](LICENSE) file for details
