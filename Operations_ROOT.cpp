
using namespace std;
using namespace CryptoPP;

map<string, vector<int>> device_rssi_history;
map<string, pair<int, int>> device_positions;

pair<int, int> calculate_position(vector<json> devices) {
    float sum_weights = 0;
    vector<float> weights;
    for (json device : devices) {
        device_rssi_history[device["id"]].push_back(device["rssi"]);
        if (device_rssi_history[device["id"]].size() > 5) {
            device_rssi_history[device["id"]].erase(device_rssi_history[device["id"]].begin());
        }
        weights.push_back(1 / pow(get_average_rssi(device["id"]), 2));
        sum_weights += weights.back();
    }

    float x = 0, y = 0;
    for (int i = 0; i < devices.size(); i++) {
        x += device_positions[devices[i]["id"]].first * weights[i] / sum_weights;
        y += device_positions[devices[i]["id"]].second * weights[i] / sum_weights;
    }
    return {x, y};
}

string encrypt_data(RSA::PublicKey publicKey, string data) {
    AutoSeededRandomPool rng;
    RSAES_OAEP_SHA_Encryptor encryptor(publicKey);
    string cipher;
    StringSource ss(data, true, new PK_EncryptorFilter(rng, encryptor, new StringSink(cipher)));
    return cipher;
}

class mqtt_handler : public mosqpp::mosquittopp {
public:
    mqtt_handler(const string& id, const string& _topic, const string& host, int port);
    virtual void on_connect(int rc);
    virtual void on_message(const struct mosquitto_message *message);
    virtual void on_subscribe(int mid, int qos_count, const int *granted_qos);

private:
    string topic;
};

mqtt_handler::mqtt_handler(const string& _id, const string& _topic, const string& host, int port)
    : mosquittopp(_id.c_str()), topic(_topic) {
    int keepalive = 60;
    connect(host.c_str(), port, keepalive);
}

void mqtt_handler::on_connect(int rc) {
    if (!rc) {
        subscribe(nullptr, topic.c_str());
    }
}

void mqtt_handler::on_message(const struct mosquitto_message *message) {
    // Your message handling code here
}

void mqtt_handler::on_subscribe(int mid, int qos_count, const int *granted_qos) {
    cout << "Subscription succeeded." << endl;
}

void LoadPublicKey(const string& filename, RSA::PublicKey& publicKey) {
    ByteQueue bytes;
    FileSource fs(filename.c_str(), true /*binary*/);
    fs.TransferTo(bytes);
    bytes.MessageEnd();
    publicKey.Load(bytes);
}

int main(int argc, char **argv) {
    string public_key_path = "public_key.pem";
    RSA::PublicKey public_key;

    LoadPublicKey(public_key_path, public_key);
    FileSink fs("key.der", true /*binary*/);
    public_key.DEREncode(fs);
    fs.MessageEnd();

    mosqpp::lib_init();

    mqtt_handler test("test", TOPIC, SERVER_ADDRESS, SERVER_PORT);

    auto forever = [&]() {
        int rc = 0;
        while(rc == 0) {
            rc = test.loop();
        }
        return rc;
    };

    thread mqtt_thread(forever);

    mqtt_thread.join();

    mosqpp::lib_cleanup();

    return 0;
}