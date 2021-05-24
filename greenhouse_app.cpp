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
#include <time.h>
#include <ctime>

#include "./json.hpp"

#include <mosquitto.h>

using json = nlohmann::json;

using namespace std;
using namespace Pistache;

// General advice: pay atetntion to the namespaces that you use in various contexts. Could prevent headaches.

// This is just a helper function to preety-print the Cookies that one of the enpoints shall receive.
void printCookies(const Http::Request &req)
{
    auto cookies = req.cookies();
    std::cout << "Cookies: [" << std::endl;
    const std::string indent(4, ' ');
    for (const auto &c : cookies)
    {
        std::cout << indent << c.name << " = " << c.value << std::endl;
    }
    std::cout << "]" << std::endl;
}

// Some generic namespace, with a simple function we could use to test the creation of the endpoints.
namespace Generic
{
    void handleReady(const Rest::Request &, Http::ResponseWriter response)
    {
        response.send(Http::Code::Ok, "2");
    }

}

struct doubleSetting
{
    std::string name;
    double value;
};

struct stringSetting
{
    std::string name;
    std::string value;
};

class ErrorHTTP
{
private:
    string error;
    Pistache::Http::Code code;

public:
    ErrorHTTP(Pistache::Http::Code code, string error)
    {
        this->code = code;
        this->error = error;
    }

    void setError(string error)
    {
        this->error = error;
    }

    void setCode(Pistache::Http::Code code)
    {
        this->code = code;
    }

    string getError() const
    {
        return error;
    }

    Pistache::Http::Code getCode() const
    {
        return code;
    }
};

class ErrorMQTT
{
private:
    string error;

public:
    ErrorMQTT(string error)
    {
        this->error = error;
    }

    void setError(string error)
    {
        this->error = error;
    }

    string getError() const
    {
        return error;
    }
};

void from_json(const json &json, ErrorHTTP &error)
{
    error.setCode(json.at("code"));
    error.setError(json.at("error"));
}

void from_json(const json &json, ErrorMQTT &error)
{
    error.setError(json.at("error"));
}

void to_json(json &j, const ErrorHTTP &error)
{
    j = json{
        {"error", error.getError()},
        {"code", error.getCode()}};
}

void to_json(json &j, const ErrorMQTT &error)
{
    j = json{
        {"error", error.getError()}};
}

struct Preconfiguration
{
    double luminosity, humidity, temperature, carbonDioxide;
    std::string plantType;
    bool operator==(const Preconfiguration &other)
    {
        return plantType == other.plantType;
    }
};

void to_json(json &j, const Preconfiguration &p)
{
    j = json{{"luminosity", p.luminosity}, {"humidity", p.humidity}, {"temperature", p.temperature}, {"carbonDioxide", p.carbonDioxide}, {"plantType", p.plantType}};
}

void from_json(const json &j, Preconfiguration &p)
{
    j.at("luminosity").get_to(p.luminosity);
    j.at("humidity").get_to(p.humidity);
    j.at("temperature").get_to(p.temperature);
    j.at("carbonDioxide").get_to(p.carbonDioxide);
    j.at("plantType").get_to(p.plantType);
}

// Definition of the GreenhouseEnpoint class
class GreenhouseEndpoint
{
public:
    explicit GreenhouseEndpoint(Address addr)
        : httpEndpoint(std::make_shared<Http::Endpoint>(addr))
    {
    }

    // Initialization of the server. Additional options can be provided here
    void init(size_t thr = 2)
    {
        auto opts = Http::Endpoint::options()
                        .threads(static_cast<int>(thr));
        httpEndpoint->init(opts);
        // Server routes are loaded up
        setupRoutes();
    }

    // Server is started threaded.
    void start()
    {
        httpEndpoint->setHandler(router.handler());
        httpEndpoint->serveThreaded();
    }

    // When signaled server shuts down
    void stop()
    {
        httpEndpoint->shutdown();
    }

    const int HTTP = 0;
    const int MQTT = 1;

    json genericAddPreconfiguration(json clientJson, int reqType)
    {
        Preconfiguration p;

        from_json(clientJson, p);

        // Setting the Greenhouse's setting to value
        int setResponse = gh.addPreconfiguration(p);

        // Sending some confirmation or error response.
        if (setResponse == 1)
        {
            if (reqType == HTTP)
            {

                ErrorHTTP error(Http::Code::Bad_Request, "The quantity must be positive!");
                json jsonErrorHttp(error);
                return jsonErrorHttp;
            }
            else
            {
                ErrorMQTT error("The quantity must be positive!");
                json jsonError(error);
                return jsonError;
            }
            // response.send(Http::Code::Ok, "Added a new preconfiguration");
        }
        else
        {
            // response.send(Http::Code::Not_Found, "Error occured. Could not add a new preconfiguration");
        }
    }

private:
    void setupRoutes()
    {
        using namespace Rest;
        // Defining various endpoints
        // Generally say that when http://localhost:9080/ready is called, the handleReady function should be called.
        Routes::Get(router, "/ready", Routes::bind(&Generic::handleReady));
        Routes::Get(router, "/auth", Routes::bind(&GreenhouseEndpoint::doAuth, this));
        Routes::Post(router, "/settings/:settingName/:value", Routes::bind(&GreenhouseEndpoint::setSetting, this));
        Routes::Get(router, "/settings/:settingName/", Routes::bind(&GreenhouseEndpoint::getSetting, this));
        Routes::Get(router, "/settings/getAll", Routes::bind(&GreenhouseEndpoint::getCurrentConfiguration, this));
        Routes::Get(router, "/waterAmount", Routes::bind(&GreenhouseEndpoint::getWaterAmountNeeded, this));
        Routes::Get(router, "/irigationTime", Routes::bind(&GreenhouseEndpoint::getIrigationTime, this));
        Routes::Get(router, "/preconfigurations/getAll", Routes::bind(&GreenhouseEndpoint::getPreconfigurations, this));
        Routes::Post(router, "/preconfigurations/select/:value", Routes::bind(&GreenhouseEndpoint::setPreconfiguration, this));
        Routes::Post(router, "/preconfigurations", Routes::bind(&GreenhouseEndpoint::addPreconfiguration, this));
        Routes::Post(router, "/soilHistory", Routes::bind(&GreenhouseEndpoint::addPlant, this));
        Routes::Get(router, "/plantType", Routes::bind(&GreenhouseEndpoint::getPlantTypeSuggestion, this));
    }

    void doAuth(const Rest::Request &request, Http::ResponseWriter response)
    {
        // Function that prints cookies
        printCookies(request);
        // In the response object, it adds a cookie regarding the communications language.
        response.cookies()
            .add(Http::Cookie("lang", "en-US"));
        // Send the response
        response.send(Http::Code::Ok);
    }

    // Endpoint to configure one of the Greenhouse's settings.
    void setSetting(const Rest::Request &request, Http::ResponseWriter response)
    {
        // You don't know what the parameter content that you receive is, but you should
        // try to cast it to some data structure. Here, I cast the settingName to string.
        auto settingName = request.param(":settingName").as<std::string>();

        // This is a guard that prevents editing the same value by two concurent threads.
        Guard guard(greenhouseLock);

        string val = "";
        if (request.hasParam(":value"))
        {
            auto value = request.param(":value");
            val = value.as<string>();
        }

        // Setting the Greenhouse's setting to value
        int setResponse = gh.set(settingName, val);

        // Sending some confirmation or error response.
        if (setResponse == 1)
        {
            response.send(Http::Code::Ok, settingName + " was set to " + val);
        }
        else
        {
            response.send(Http::Code::Not_Found, settingName + " was not found and or '" + val + "' was not a valid value ");
        }
    }

    void addPreconfiguration(const Rest::Request &request, Http::ResponseWriter response)
    {
        // You don't know what the parameter content that you receive is, but you should
        // try to cast it to some data structure. Here, I cast the settingName to string.
        string requestSettings = request.body();

        json j = json::parse(requestSettings);

        // This is a guard that prevents editing the same value by two concurent threads.
        Guard guard(greenhouseLock);
    }

    void addPlant(const Rest::Request &request, Http::ResponseWriter response)
    {
        // You don't know what the parameter content that you receive is, but you should
        // try to cast it to some data structure. Here, I cast the settingName to string.
        string requestSettings = request.body();

        json j = json::parse(requestSettings);

        // This is a guard that prevents editing the same value by two concurent threads.
        Guard guard(greenhouseLock);

        std::string plant;
        j.at("plantType").get_to(plant);

        // Setting the Greenhouse's setting to value
        int setResponse = gh.addPlant(plant);

        // Sending some confirmation or error response.
        if (setResponse == 1)
        {
            response.send(Http::Code::Ok, "Added a new plant to soil history");
        }
        else
        {
            response.send(Http::Code::Not_Found, "Error occured. Could not add a new plant to soil history");
        }
    }

    // Setting to get the settings value of one of the configurations of the Greenhouse
    void getSetting(const Rest::Request &request, Http::ResponseWriter response)
    {
        auto settingName = request.param(":settingName").as<std::string>();

        Guard guard(greenhouseLock);

        string valueSetting = gh.get(settingName);

        if (valueSetting != "")
        {

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                .add<Header::Server>("pistache/0.1")
                .add<Header::ContentType>(MIME(Text, Plain));

            response.send(Http::Code::Ok, settingName + " is " + valueSetting);
        }
        else
        {
            response.send(Http::Code::Not_Found, settingName + " was not found");
        }
    }

    void getCurrentConfiguration(const Rest::Request &request, Http::ResponseWriter response)
    {

        Guard guard(greenhouseLock);

        string stringJSON = gh.getCurrentConfiguration();

        if (stringJSON != "")
        {

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                .add<Header::Server>("pistache/0.1")
                .add<Header::ContentType>(MIME(Text, Plain));

            response.send(Http::Code::Ok, stringJSON);
        }
        else
        {
            response.send(Http::Code::Not_Found, "An error has occured.");
        }
    }

    void getWaterAmountNeeded(const Rest::Request &request, Http::ResponseWriter response)
    {

        Guard guard(greenhouseLock);

        string stringJSON = gh.calculateWaterAmount();

        if (stringJSON != "")
        {

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                .add<Header::Server>("pistache/0.1")
                .add<Header::ContentType>(MIME(Text, Plain));

            response.send(Http::Code::Ok, stringJSON);
        }
        else
        {
            response.send(Http::Code::Not_Found, "An error has occured.");
        }
    }

    void getIrigationTime(const Rest::Request &request, Http::ResponseWriter response)
    {

        Guard guard(greenhouseLock);

        string stringJSON = gh.calculateIrigationTime();

        if (stringJSON != "")
        {

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                .add<Header::Server>("pistache/0.1")
                .add<Header::ContentType>(MIME(Text, Plain));

            response.send(Http::Code::Ok, stringJSON);
        }
        else
        {
            response.send(Http::Code::Not_Found, "An error has occured.");
        }
    }

    void getPreconfigurations(const Rest::Request &request, Http::ResponseWriter response)
    {

        Guard guard(greenhouseLock);

        string stringJSON = gh.preconfigurationsToJSON();

        if (stringJSON != "")
        {

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                .add<Header::Server>("pistache/0.1")
                .add<Header::ContentType>(MIME(Text, Plain));

            response.send(Http::Code::Ok, stringJSON);
        }
        else
        {
            response.send(Http::Code::Not_Found, "An error has occured.");
        }
    }

    void setPreconfiguration(const Rest::Request &request, Http::ResponseWriter response)
    {
        // You don't know what the parameter content that you receive is, but you should
        // try to cast it to some data structure. Here, I cast the settingName to string.
        int nrConfig = request.param(":value").as<int>();

        // This is a guard that prevents editing the same value by two concurent threads.
        Guard guard(greenhouseLock);

        // Setting the Greenhouse's setting to value
        int setResponse = gh.setPreconfiguration(nrConfig);

        // Sending some confirmation or error response.
        if (setResponse == 1)
        {
            response.send(Http::Code::Ok, "Configuration " + to_string(nrConfig) + " was applied");
        }
        else
        {
            response.send(Http::Code::Not_Found, "The preconfiguration " + to_string(nrConfig) + " was not found");
        }
    }

    void getPlantTypeSuggestion(const Rest::Request &request, Http::ResponseWriter response)
    {

        Guard guard(greenhouseLock);

        string stringJSON = gh.getPlantTypeSuggestion();

        if (stringJSON != "")
        {

            // In this response I also add a couple of headers, describing the server that sent this response, and the way the content is formatted.
            using namespace Http;
            response.headers()
                .add<Header::Server>("pistache/0.1")
                .add<Header::ContentType>(MIME(Text, Plain));

            response.send(Http::Code::Ok, stringJSON);
        }
        else
        {
            response.send(Http::Code::Not_Found, "An error has occured.");
        }
    }

    // Defining the class of the Greenhouse. It should model the entire configuration of the Greenhouse
    class Greenhouse
    {
    public:
        explicit Greenhouse()
        {
            humidity.name = "humidity";
            luminosity.name = "luminosity";
            temperature.name = "temperature";
            carbonDioxide.name = "carbonDioxide";
            area.name = "area";
            waterAmount.name = "waterAmount";
            plantType.name = "plantType";

            humidity.value = 0;
            luminosity.value = 0;
            temperature.value = 0;
            carbonDioxide.value = 0;
            area.value = 0;
            waterAmount.value = 0;
            plantType.value = "";

            readSoilHistory();
            readIdealParameters();
            readPreconfigurations();
            setPreconfiguration(0);
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

        void readPreconfigurations()
        {
            ifstream fin(preconfigurationsLocation);
            int nrPreconfigurations;
            fin >> nrPreconfigurations;
            for (int i = 0; i < nrPreconfigurations; i++)
            {
                Preconfiguration p;
                fin >> p.luminosity >> p.humidity >> p.temperature >> p.carbonDioxide >> p.plantType;
                preconfigurations.push_back(p);
            }
        }

        int setPreconfiguration(int nrPreconfig)
        {
            if (nrPreconfig >= preconfigurations.size())
            {
                return -1;
            }

            Preconfiguration &p = preconfigurations[nrPreconfig];
            luminosity.value = p.luminosity;
            humidity.value = p.humidity;
            temperature.value = p.temperature;
            carbonDioxide.value = p.carbonDioxide;
            plantType.value = p.plantType;

            return 1;
        }

        string preconfigurationsToJSON()
        {
            json j(preconfigurations);

            return j.dump();
        }

        string getPlantTypeSuggestion()
        {
            map<std::string, int> plants;

            for (int i = 0; i < soilHistory.size(); i++)
            {
                auto it = plants.find(soilHistory[i]);
                if (it != plants.end())
                    plants[soilHistory[i]] += 1;
                else
                    plants[soilHistory[i]] = 1;
            }
            if (plantType.value != "")
                plants[plantType.value] += 1;

            int minim = INT_MAX - 1;
            std::string pos = "";
            for (int i = 0; i < soilHistory.size(); i++)
                if (plants[soilHistory[i]] < minim)
                {
                    minim = plants[soilHistory[i]];
                    pos = soilHistory[i];
                }
            json j;
            j["suggestedPlant"] = pos;
            return j.dump();
        }

        // Setting the value for one of the settings. Hardcoded for the defrosting option
        int set(std::string name, std::string value)
        {
            if (luminosity.name == name)
            {
                try
                {
                    double doubleValue = std::stod(value);
                    if (doubleValue >= 0 && doubleValue <= 100)
                    {
                        luminosity.value = doubleValue;
                        return 1;
                    }
                }
                catch (std::exception)
                {
                    return 0;
                }
            }

            if (humidity.name == name)
            {
                try
                {
                    double doubleValue = std::stod(value);
                    if (doubleValue >= 0 && doubleValue <= 100)
                    {
                        humidity.value = doubleValue;
                        return 1;
                    }
                }
                catch (std::exception)
                {
                    return 0;
                }
            }

            if (temperature.name == name)
            {
                try
                {
                    double doubleValue = std::stod(value);
                    if (doubleValue >= 5 && doubleValue <= 35)
                    {
                        temperature.value = doubleValue;
                        return 1;
                    }
                }
                catch (std::exception)
                {
                    return 0;
                }
            }

            if (carbonDioxide.name == name)
            {
                try
                {
                    double doubleValue = std::stod(value);
                    if (doubleValue >= 0 && doubleValue <= 100)
                    {
                        carbonDioxide.value = doubleValue;
                        return 1;
                    }
                }
                catch (std::exception)
                {
                    return 0;
                }
            }

            if (area.name == name)
            {
                try
                {
                    double doubleValue = std::stod(value);
                    if (doubleValue >= 0)
                    {
                        area.value = doubleValue;
                        return 1;
                    }
                }
                catch (std::exception)
                {
                    return 0;
                }
            }

            if (waterAmount.name == name)
            {
                try
                {
                    double doubleValue = std::stod(value);
                    if (doubleValue >= 0)
                    {
                        waterAmount.value = doubleValue;
                        return 1;
                    }
                }
                catch (std::exception)
                {
                    return 0;
                }
            }

            if (plantType.name == name)
            {
                plantType.value = value;
                return 1;
            }

            return 0;
        }

        // Getter
        string get(std::string name)
        {
            if (name == luminosity.name)
            {
                return std::to_string(luminosity.value);
            }
            if (name == humidity.name)
            {
                return std::to_string(humidity.value);
            }
            if (name == temperature.name)
            {
                return std::to_string(temperature.value);
            }
            if (name == carbonDioxide.name)
            {
                return std::to_string(carbonDioxide.value);
            }
            if (name == area.name)
            {
                return std::to_string(area.value);
            }
            if (name == waterAmount.name)
            {
                return std::to_string(waterAmount.value);
            }
            if (name == plantType.name)
            {
                return plantType.value;
            }

            return "";
        }

        string getCurrentConfiguration()
        {
            json j;
            j["luminosity"] = luminosity.value;
            j["humidity"] = humidity.value;
            j["temperature"] = temperature.value;
            j["carbonDioxide"] = carbonDioxide.value;
            j["area"] = area.value;
            j["waterAmount"] = waterAmount.value;
            j["plantType"] = plantType.value;

            return j.dump();
        }

        string calculateWaterAmount()
        {
            double result;
            json j;
            if (temperature.value < 25)
                result = area.value * 0.7;
            else if (temperature.value >= 25 && temperature.value <= 28)
                result = area.value * 0.8;
            else
                result = area.value * 0.9;

            j["waterAmount"] = result;

            return j.dump();
        }

        string calculateIrigationTime()
        {
            std::string response = "";

            struct tm newtime;
            time_t now = time(0);

            newtime = *localtime(&now);

            std::string day = "";
            int count = 0;

            if (newtime.tm_mday % 2 == 0)
            {
                day = "Tomorrow, ";
                count = 1;
            }
            else
            {
                std::string currentTime = to_string(newtime.tm_hour) + "/" + to_string(newtime.tm_min) +
                                          to_string(newtime.tm_sec);

                const char *irigationTime = "7:0:0";

                struct tm irigationTimeTransformed = {0};
                strptime(irigationTime, "%H:%M:%S", &irigationTimeTransformed);
                time_t irigation = mktime(&irigationTimeTransformed);

                if (now < irigation)
                {
                    day = "Today, ";
                    count = 0;
                }

                else
                {
                    day = "After 2 days, ";
                    count = 2;
                }
            }

            response = day + to_string(newtime.tm_mday + count) + "/" + to_string(newtime.tm_mon + 1) + "/" +
                       to_string(newtime.tm_year + 1900) + ", " + "07:00:00 AM";

            json j;
            j["irigationTime"] = response;
            return j.dump();
        }

        int addPreconfiguration(Preconfiguration p)
        {
            if (std::find(preconfigurations.begin(), preconfigurations.end(), p) != preconfigurations.end())
            {
                /* v contains x */
                return -1;
            }
            else
            {
                /* v does not contain x */
                preconfigurations.push_back(p);
                return 1;
            }
        }

        int addPlant(std::string plant)
        {
            soilHistory.push_back(plant);
            return 1;
        }

    private:
        struct parameters
        {
            double luminosity;
            double humidity;
            double temperature;
            double carbonDioxide;
        } ideal_parameters;

        doubleSetting luminosity, humidity, temperature, carbonDioxide, area, waterAmount;
        stringSetting plantType;

        map<std::string, std::string> actions;
        vector<std::string> soilHistory;
        vector<Preconfiguration> preconfigurations;
        const std::string soilHistoryLocation = "soil_history.txt";
        const std::string idealParametersLocation = "ideal_parameters.txt";
        const std::string preconfigurationsLocation = "preconfigurations.txt";

        // temporary
        // Defining and instantiating settings.
        struct boolSetting
        {
            std::string name;
            bool value;
        } defrost;
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

int main(int argc, char *argv[])
{

    // This code is needed for gracefull shutdown of the server when no longer needed.
    sigset_t signals;
    if (sigemptyset(&signals) != 0 || sigaddset(&signals, SIGTERM) != 0 || sigaddset(&signals, SIGINT) != 0 || sigaddset(&signals, SIGHUP) != 0 || pthread_sigmask(SIG_BLOCK, &signals, nullptr) != 0)
    {
        perror("install signal handler failed");
        return 1;
    }

    // Set a port on which your server to communicate
    Port port(9080);

    // Number of threads used by the server
    int thr = 2;

    if (argc >= 2)
    {
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

    struct mosquitto *mosq;

    int pid = fork();
    if (pid == 0)
    {

        mosquitto_lib_init();

        mosq = mosquitto_new("publisher", true, NULL);
        int rc = mosquitto_connect(mosq, "localhost", 1883, 60);
        if (rc != 0)
        {
            printf("\nError with code = %d\n", rc);
            mosquitto_destroy(mosq);
            return -1;
        }
        while (1)
        {
            // json response = stats.genericAddPreconfiguration(GetCurrentFruits(), Generic::MQTT);
            // string stringJuice = juice.dump();
            // mosquitto_publish(mosq, NULL, "mqtt", stringJuice.size(), stringJuice.c_str(), 0, false);
            sleep(20);
        }
    }

    // Code that waits for the shutdown sinal for the server
    int signal = 0;
    int status = sigwait(&signals, &signal);
    if (status == 0)
    {
        mosquitto_disconnect(mosq);
        mosquitto_lib_cleanup();
        mosquitto_destroy(mosq);
        std::cout << "received signal " << signal << std::endl;
    }
    else
    {
        std::cerr << "sigwait returns " << status << std::endl;
    }

    stats.stop();
}