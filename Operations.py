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
    "ESP32-02": (4,0), 
    "ESP32-03": (2,2)
}

def calculate_position(devices):
    print("Calculating position...")
    weights = [1/(device['rssi']**2) for device in devices] # Squared RSSI to emphasize the effect
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
