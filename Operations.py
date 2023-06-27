import csv
import paho.mqtt.client as mqtt
import json
import time
import hashlib
from cryptography.hazmat.primitives import hashes
from cryptography.hazmat.primitives.asymmetric import padding
from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.serialization import load_pem_public_key
from cryptography.hazmat.backends import default_backend
from concurrent.futures import ThreadPoolExecutor
import base64

mqtt_server = "192.168.31.124"
mqtt_port = 1883
topic = "/ble/scannedDevices/#"
public_key_path = "public_key.pem"
max_workers = 4  # number of CPU cores to be utilized

device_positions = {
    "ESP32-01": (0,0), 
    "ESP32-02": (8,0), 
    "ESP32-03": (4,4)
}

device_rssi_history = {
    "ESP32-01": [], 
    "ESP32-02": [], 
    "ESP32-03": []
}

def get_average_rssi(device):
    history = device_rssi_history[device['id']]
    return sum(history) / len(history)

def calculate_position(devices):
    print("Calculating position...")
    
    for device in devices:
        device_rssi_history[device['id']].append(device['rssi'])
        device_rssi_history[device['id']] = device_rssi_history[device['id']][-5:]
        
    weights = [1/(get_average_rssi(device)**2) for device in devices]
    sum_weights = sum(weights)
    norm_weights = [weight/sum_weights for weight in weights]
    
    x = sum(device_positions[device['id']][0] * weight for device, weight in zip(devices, norm_weights))
    y = sum(device_positions[device['id']][1] * weight for device, weight in zip(devices, norm_weights))
    print(f"Position calculated: {x, y}")
    
    return (x, y)

def exhaustive_hashing(data, iterations=1000000):
    hashed_data = data.encode()
    
    for i in range(iterations):
        hashed_data = hashlib.sha512(hashed_data).digest()
        
    return hashed_data

def encrypt_data(public_key, data):
    hashed_data = exhaustive_hashing(data)
    encrypted = public_key.encrypt(
    hashed_data,
    padding.OAEP(
        mgf=padding.MGF1(algorithm=hashes.SHA512()),
        algorithm=hashes.SHA512(),
        label=None
        )
    )
    print("Data encrypted.")
    return base64.b64encode(encrypted).decode()

def on_connect(client, userdata, flags, rc):
    print(f"Connected with result code {rc}")
    client.subscribe(topic)

def on_message(client, userdata, msg):
    print(msg.topic + " " + str(msg.payload))
    data = json.loads(msg.payload)
    devices = data['ESP32C3']
    timestamp = time.strftime("%Y-%m-%d %H:%M:%S", time.gmtime())

    with open('data.csv', 'a', newline='') as file:
        writer = csv.writer(file)
        for device in devices:
            print(f"Appending {device['id']} to 'data.csv'")
            writer.writerow([timestamp, device['id'], device['rssi']])

    position = calculate_position(devices)

    # Use a thread pool for concurrent encryption
    with ThreadPoolExecutor(max_workers=max_workers) as executor:
        encrypted_position = executor.submit(encrypt_data, public_key, f"{timestamp},{position}").result()

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
