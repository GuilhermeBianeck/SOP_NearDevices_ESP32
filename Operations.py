import csv
import paho.mqtt.client as mqtt
import paho.mqtt.subscribe as subscribe
import json
import time
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives import hashes
import base64

mqtt_server = "192.168.31.124"
mqtt_port = 1883
topic = "/ble/scannedDevices/#"
public_key_path = "public_key.pem"

device_positions = {
    "ESP32-01": (0,0), 
    "ESP32-02": (8,0), 
    "ESP32-03": (4,4)
}

# Function to keep track of history of RSSI values for each device
device_rssi_history = {
    "ESP32-01": [], 
    "ESP32-02": [], 
    "ESP32-03": []
}

# Function to compute the average of the RSSI history
def get_average_rssi(device):
    history = device_rssi_history[device['id']]
    return sum(history) / len(history)

def calculate_position(devices):
    print("Calculating position...")
    
    # Store the latest RSSI values to the history
    for device in devices:
        device_rssi_history[device['id']].append(device['rssi'])
        # Keep only the last 5 values
        device_rssi_history[device['id']] = device_rssi_history[device['id']][-5:]
        
    # Compute the weights using the average RSSI value from the history
    weights = [1/(get_average_rssi(device)**2) for device in devices]
    sum_weights = sum(weights)
    norm_weights = [weight/sum_weights for weight in weights]
    
    x = sum(device_positions[device['id']][0] * weight for device, weight in zip(devices, norm_weights))
    y = sum(device_positions[device['id']][1] * weight for device, weight in zip(devices, norm_weights))
    print(f"Position calculated: {x, y}")
    
    return (x, y)


def encrypt_data(public_key, data):
    print("Encrypting data...")
    encrypted = public_key.encrypt(
        data.encode(),
        padding.OAEP(
            mgf=padding.MGF1(algorithm=hashes.SHA256()),
            algorithm=hashes.SHA256(),
            label=None
        )
    )
    print("Data encrypted.")
    return base64.b64encode(encrypted).decode()  # return as string

def on_connect(client, userdata, flags, rc):
    print(f"Connected with result code {rc}")
    client.subscribe(topic)

def on_message(client, userdata, msg):
    print(msg.topic + " " + str(msg.payload))
    data = json.loads(msg.payload)
    devices = data['ESP32C3']
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime())

    # Append to CSV
    with open('data.csv', 'a', newline='') as file:
        writer = csv.writer(file)
        for device in devices:
            print(f"Appending {device['id']} to 'data.csv'")
            writer.writerow([timestamp, device['id'], device['rssi']])

    # Calculate position and append to another CSV
    position = calculate_position(devices)
    encrypted_position = encrypt_data(public_key, f"{timestamp},{position}")
    with open('position.csv', 'a', newline='') as file:
        writer = csv.writer(file)
        print(f"Appending encrypted position to 'position.csv'")
        writer.writerow([timestamp, "ESP32C3", encrypted_position])

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message

with open(public_key_path, "rb") as key_file:
    public_key = serialization.load_pem_public_key(
        key_file.read()
    )
    
print("Connecting to MQTT server...")
client.connect(mqtt_server, mqtt_port, 60)

print("Starting MQTT loop...")
client.loop_forever()
