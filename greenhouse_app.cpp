/*
   Rares Cristea, 12.03.2021
   Example of a REST endpoint with routing
   using Mathieu Stefani's example, 07 février 2016
*/

#include <algorithm>

#include <pistache/net.h>
#include <pistache/http.h>
#include <pistache/peer.h>
#include <pistache/http_headers.h>
#include <pistache/cookie.h>
#include <pistache/router.h>
#include <pistache/endpoint.h>
#include <pistache/common.h>

#include <signal.h>
#include <map>
#include <list>
#include <fstream>

// #include <nlohmann/json.hpp>

// using json = nlohmann::json;

using namespace std;
using namespace Pistache;

// General advice: pay atetntion to the namespaces that you use in various contexts. Could prevent headaches.

// This is just a helper function to preety-print the Cookies that one of the enpoints shall receive.
void printCookies(const Http::Request& req) {
    auto cookies = req.cookies();
    std::cout << "Cookies: [" << std::endl;
    const std::string indent(4, ' ');
    for (const auto& c: cookies) {
        std::cout << indent << c.name << " = " << c.value << std::endl;
    }
    std::cout << "]" << std::endl;
}

// Some generic namespace, with a simple function we could use to test the creation of the endpoints.
namespace Generic {

    void handleReady(const Rest::Request&, Http::ResponseWriter response) {
        response.send(Http::Code::Ok, "2");
    }

}

struct doubleSetting {
    std::string name;
    int value;
};

struct stringSetting {
    std::string name;
    std::string value;
};

// Definition of the GreenhouseEnpoint class 
class GreenhouseEndpoint {
public:
    explicit GreenhouseEndpoint(Address addr)
        : httpEndpoint(std::make_shared<Http::Endpoint>(addr))
    { }

    // Initialization of the server. Additional options can be provided here
    void init(size_t thr = 2) {
        auto opts = Http::Endpoint::options()
            .threads(static_cast<int>(thr));
        httpEndpoint->init(opts);
        // Server routes are loaded up
        setupRoutes();
    }

    // Server is started threaded.  
    void start() {
        httpEndpoint->setHandler(router.handler());
        httpEndpoint->serveThreaded();
    }

    // When signaled server shuts down
    void stop(){
        httpEndpoint->shutdown();
    }

private:
    void setupRoutes() {
        using namespace Rest;
        // Defining various endpoints
        // Generally say that when http://localhost:9080/ready is called, the handleReady function should be called. 
        Routes::Get(router, "/ready", Routes::bind(&Generic::handleReady));
        Routes::Get(router, "/auth", Routes::bind(&GreenhouseEndpoint::doAuth, this));
        Routes::Post(router, "/settings/:settingName/:value", Routes::bind(&GreenhouseEndpoint::setSetting, this));
        Routes::Get(router, "/settings/:settingName/", Routes::bind(&GreenhouseEndpoint::getSetting, this));
    }

    
    void doAuth(const Rest::Request& request, Http::ResponseWriter response) {
        // Function that prints cookies
        printCookies(request);
        // In the response object, it adds a cookie regarding the communications language.
        response.cookies()
            .add(Http::Cookie("lang", "en-US"));
        // Send the response
        response.send(Http::Code::Ok);
    }

    // Endpoint to configure one of the Greenhouse's settings.
    void setSetting(const Rest::Request& request, Http::ResponseWriter response){
        // You don't know what the parameter content that you receive is, but you should
        // try to cast it to some data structure. Here, I cast the settingName to string.
        auto settingName = request.param(":settingName").as<std::string>();

        // This is a guard that prevents editing the same value by two concurent threads. 
        Guard guard(greenhouseLock);

        
        string val = "";
        if (request.hasParam(":value")) {
            auto value = request.param(":value");
            val = value.as<string>();
        }

        // Setting the Greenhouse's setting to value
        int setResponse = gh.set(settingName, val);

        // Sending some confirmation or error response.
        if (setResponse == 1) {
            response.send(Http::Code::Ok, settingName + " was set to " + val);
        }
        else {
            response.send(Http::Code::Not_Found, settingName + " was not found and or '" + val + "' was not a valid value ");
        }

    }

    // Setting to get the settings value of one of the configurations of the Greenhouse
    void getSetting(const Rest::Request& request, Http::ResponseWriter response){
        auto settingName = request.param(":settingName").as<std::string>();

        Guard guard(greenhouseLock);

        string valueSetting = gh.get(settingName);

        if (valueSetting != "") {

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                        .add<Header::Server>("pistache/0.1")
                        .add<Header::ContentType>(MIME(Text, Plain));

            response.send(Http::Code::Ok, settingName + " is " + valueSetting);
        }
        else {
            response.send(Http::Code::Not_Found, settingName + " was not found");
        }
    }

    // Defining the class of the Greenhouse. It should model the entire configuration of the Greenhouse
    class Greenhouse {
    public:
        explicit Greenhouse(){ 
            humidity.name = "humidity";
            luminosity.name = "luminosity";
            temperature.name = "temperature";
            carbonDioxide.name = "carbonDioxide";
            area.name = "area";
            waterAmount.name = "waterAmount";
            plantType.name = "plantType";

            readSoilHistory();
            readIdealParameters();
        }
        
        void readSoilHistory()
        {
            ifstream fin(soilHistoryLocation);
            int nrYears;
            fin >> nrYears;
            for (int i = 0; i < nrYears; i++)
            {
                std::string plant;
                fin >> plant;
                soilHistory.push_back(plant);
            }

        }

        void readIdealParameters()
        {
            ifstream fin(idealParametersLocation);
            double value[4];
            fin >> value[0] >> value[1] >> value[2] >> value[3];
            ideal_parameters.luminosity = value[0];
            ideal_parameters.humidity = value[1];
            ideal_parameters.temperature = value[2];
            ideal_parameters.carbonDioxide = value[3];
        }

        // Setting the value for one of the settings. Hardcoded for the defrosting option
        int set(std::string name, std::string value){
            if (luminosity.name == name) {
                try 
                {
                    double doubleValue = std::stod(value);
                    if (doubleValue >= 0 && doubleValue <= 100)
                    {
                        luminosity.value = doubleValue;
                        return 1;
                    }
                }
                catch(std::exception)
                {
                    return 0;
                }
            }

            if (humidity.name == name) {
                try 
                {
                    double doubleValue = std::stod(value);
                    if (doubleValue >= 0 && doubleValue <= 100)
                    {
                        humidity.value = doubleValue;
                        return 1;
                    }
                }
                catch(std::exception)
                {
                    return 0;
                }
            }

            if (temperature.name == name) {
                try 
                {
                    double doubleValue = std::stod(value);
                    if (doubleValue >= 5 && doubleValue <= 35)
                    {
                        temperature.value = doubleValue;
                        return 1;
                    }
                }
                catch(std::exception)
                {
                    return 0;
                }
            }

            if (carbonDioxide.name == name) {
                try 
                {
                    double doubleValue = std::stod(value);
                    if (doubleValue >= 0 && doubleValue <= 100)
                    {
                        temperature.value = doubleValue;
                        return 1;
                    }
                }
                catch(std::exception)
                {
                    return 0;
                }
            }

            if (area.name == name) {
                try 
                {
                    double doubleValue = std::stod(value);
                    if (doubleValue >= 0)
                    {
                        area.value = doubleValue;
                        return 1;
                    }
                }
                catch(std::exception)
                {
                    return 0;
                }
            }

            if (waterAmount.name == name) {
                try 
                {
                    double doubleValue = std::stod(value);
                    if (doubleValue >= 0)
                    {
                        waterAmount.value = doubleValue;
                        return 1;
                    }
                }
                catch(std::exception)
                {
                    return 0;
                }
            }

             if (plantType.name == name) {
                plantType.value = value;
                return 1;
            }

            return 0;
        }

        // Getter
        string get(std::string name){
            if (name == luminosity.name) {
                return std::to_string(luminosity.value);
            }
            if (name == humidity.name) {
                return std::to_string(humidity.value);
            }
            if (name == temperature.name) {
                return std::to_string(temperature.value);
            }
            if (name == carbonDioxide.name) {
                return std::to_string(carbonDioxide.value);
            }
            if (name == area.name) {
                return std::to_string(area.value);
            }
            if (name == waterAmount.name) {
                return std::to_string(waterAmount.value);
            }
            if (name == plantType.name) {
                return plantType.value;
            }
        }
        
    private:
        struct parameters {
            double luminosity;
            double humidity;
            double temperature;
            double carbonDioxide;
        }ideal_parameters;

        doubleSetting luminosity, humidity, temperature, carbonDioxide, area, waterAmount;
        stringSetting plantType;

        map<std::string, std::string> actions;
        list<std::string> soilHistory;
        const std::string soilHistoryLocation = "soil_history.txt";
        const std::string idealParametersLocation = "ideal_parameters.txt";


        // temporary
        // Defining and instantiating settings.
        struct boolSetting{
            std::string name;
            bool value;
        }defrost;
    };
    // Create the lock which prevents concurrent editing of the same variable
    using Lock = std::mutex;
    using Guard = std::lock_guard<Lock>;
    Lock greenhouseLock;

    // Instance of the Greenhouse model
    Greenhouse gh;

    // Defining the httpEndpoint and a router.
    std::shared_ptr<Http::Endpoint> httpEndpoint;
    Rest::Router router;
};

int main(int argc, char *argv[]) {

    // This code is needed for gracefull shutdown of the server when no longer needed.
    sigset_t signals;
    if (sigemptyset(&signals) != 0
            || sigaddset(&signals, SIGTERM) != 0
            || sigaddset(&signals, SIGINT) != 0
            || sigaddset(&signals, SIGHUP) != 0
            || pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0) {
        perror("install signal handler failed");
        return 1;
    }

    // Set a port on which your server to communicate
    Port port(9080);

    // Number of threads used by the server
    int thr = 2;

    if (argc >= 2) {
        port = static_cast<uint16_t>(std::stol(argv[1]));

        if (argc == 3)
            thr = std::stoi(argv[2]);
    }

    Address addr(Ipv4::any(), port);

    cout << "Cores = " << hardware_concurrency() << endl;
    cout << "Using " << thr << " threads" << endl;

    // Instance of the class that defines what the server can do.
    GreenhouseEndpoint stats(addr);

    // Initialize and start the server
    stats.init(thr);
    stats.start();

    // Code that waits for the shutdown sinal for the server
    int signal = 0;
    int status = sigwait(&signals, &signal);
    if (status == 0)
    {
        std::cout << "received signal " << signal << std::endl;
    }
    else
    {
        std::cerr << "sigwait returns " << status << std::endl;
    }

    stats.stop();
}