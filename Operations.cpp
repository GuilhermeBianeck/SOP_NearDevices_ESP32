#include <chrono>
#include <future>
#include <iostream>
#include <vector>
#include <map>
#include <cmath>

#include "mqtt/async_client.h"
#include "json.hpp"
#include "cryptopp/rsa.h"
#include "cryptopp/pem.h"
#include "cryptopp/osrng.h"
#include "cryptopp/base64.h"
#include "cryptopp/filters.h"
#include "cryptopp/files.h"
#include "cryptopp/sha.h"
#include "csv2/csv.hpp"

using namespace std;
using namespace CryptoPP;
using json = nlohmann::json;
using namespace csv2;

const auto SERVER_ADDRESS = "tcp://192.168.31.124:1883"s;
const auto TOPIC = "/ble/scannedDevices/#"s;

const map<string, pair<int, int>> DEVICE_POSITIONS = {
    {"ESP32-01", {0, 0}},
    {"ESP32-02", {8, 0}},
    {"ESP32-03", {4, 4}}
};

map<string, vector<int>> device_rssi_history = {
    {"ESP32-01", {}},
    {"ESP32-02", {}},
    {"ESP32-03", {}}
};

float get_average_rssi(const string& device_id) {
    const auto& history = device_rssi_history[device_id];
    return accumulate(begin(history), end(history), 0.0f) / history.size();
}

pair<int, int> calculate_position(const vector<json>& devices) {
    vector<float> weights;
    float sum_weights = 0.0f;

    for (const auto& device : devices) {
        auto& history = device_rssi_history[device["id"]];
        history.push_back(device["rssi"]);
        if (history.size() > 5) history.erase(begin(history));

        const auto weight = 1 / pow(get_average_rssi(device["id"]), 2);
        weights.push_back(weight);
        sum_weights += weight;
    }

    auto x = 0.0f, y = 0.0f;
    for (size_t i = 0; i < devices.size(); i++) {
        const auto& pos = DEVICE_POSITIONS.at(devices[i]["id"]);
        x += pos.first * weights[i] / sum_weights;
        y += pos.second * weights[i] / sum_weights;
    }

    return {x, y};
}

string encrypt_data(const RSA::PublicKey& publicKey, const string& data) {
    AutoSeededRandomPool rng;
    RSAES_OAEP_SHA_Encryptor encryptor(publicKey);
    string cipher;
    StringSource ss(data, true, new PK_EncryptorFilter(rng, encryptor, new StringSink(cipher)));
    return cipher;
}

int main() {
    const auto public_key_path = "public_key.pem"s;
    RSA::PublicKey public_key;

    PEM_Load(public_key_path.c_str(), public_key);
    FileSink fs("key.der", true /*binary*/);
    public_key.DEREncode(fs);
    fs.MessageEnd();

    mqtt::async_client cli(SERVER_ADDRESS, "");

    const auto connOpts = mqtt::connect_options_builder().clean_session(true).finalize();
    cli.connect(connOpts)->wait();

    cli.start_consuming();
    cli.subscribe(TOPIC, 1)->wait();

    while (true) {
        const auto msg = cli.consume_message();
        if (msg) {
            const auto data = json::parse(msg->to_string());
            const auto devices = data["ESP32C3"];
            const auto timestamp = to_string(chrono::system_clock::to_time_t(
