
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

WiFiClient espClient;
PubSubClient client(espClient);
StaticJsonDocument<200> doc;


const char* ssid = "XXX";
const char* password = "XXX";
const char* mqtt_server = "XXX";
const char* configure_topic = "init/0001";
const char* error_topic = "sensor/error";
const char* connect_topic = "sensor/connect";
const char* info_topic = "sensor/info";
const char* value_topic = "sensor/distance";

const char* mqtt_username = "mosquitto";
const char* mqtt_password = "mosquitto";
const int mqtt_port = 1883;

const int trigger = 0;
const int echo = 2;

// Create a random client ID
String clientId = "SmartObject-Zysterne-";

//Inital Setup
int height = 180;
int radius = 109;
bool debug_mode = true;
int num_readings = 5;

//Messages
const char* error_sensor_id = "| Something is wrong with my sensor_id. I am measuring more than maximum height or less than minimum. I need help.";
String msg_connected = " | I am connected.";
String msg_subscribed = "| I am subscribed to configure topic.";
String msg_measuring = "| I am measuring.";
String msg_nothing_changed = "| Nothing changed, but I will still store it in database.";

//Subscribe Stuff
String sensor_id;

//we will use a non-blocking delay to be able to call client.loop()
int sample_rate = 20000;
unsigned long time_now = 0;
int thisReading = 0;
float maximum = 0;

//Distance Stuff
float Pi = 3.14159;

long duration = 0;
float distance = 0;



void loop() {
  //Very important use non-blocking delays
  if (millis() > time_now + sample_rate) {
    time_now = millis();
    if (thisReading < num_readings) {
      measureDistance();
      thisReading++;
    } else {
       publishToBroker(distance);
      thisReading = 0;
      maximum = 0;
    }
  }
  if (!client.connected()) {
    reconnect();
  }
  //I am looping for MQTT Keepalive - important otherwise you will get socket errors
  client.loop();
  delay(1000);
}


void measureDistance() {
  // read from the sensor_id:
  digitalWrite(trigger, LOW);
  delay(5);
  digitalWrite(trigger, HIGH);
  delay(10);
  digitalWrite(trigger, LOW);
  float measurement = pulseIn(echo, HIGH);
  maximum = _max(maximum, measurement);
  distance = (maximum / 2) / 29.1;
  float waterlevel = height - distance;
  String msg_measuring_with_client_id = clientId + msg_measuring + " " + thisReading + "/" + num_readings + "(" + waterlevel + " Waterlevel)";
  publishInfoLogging(msg_measuring_with_client_id);
}



void setup_wifi() {
  delay(10);
  // We start by connecting to a WiFi network
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  randomSeed(micros());
  Serial.print("WiFi connected ");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.println("I have received a message from subscription.");
  char payloadChar[length];
  for (int i = 0; i < length; i++) {
    payloadChar[i] = (char)payload[i];
  }
  DeserializationError error = deserializeJson(doc, payloadChar);

  // Test if parsing succeeds.
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.c_str());
    return;
  }
  Serial.println();

  // Get the root object in the document
  JsonObject root = doc.as<JsonObject>();

  String sensor_id_received = root["id"];
  sensor_id = sensor_id_received;

  int interval_received = root["i"];
  sample_rate = interval_received;

  int radius_received = root["r"];
  radius = radius_received;

  int height_received = root["h"];
  height = height_received;

  int num_reads_received = root["nr"];
  num_readings = num_reads_received;

  bool debug_mode_received = root["dm"];
  debug_mode = debug_mode_received;

  publishInfoToBroker(sensor_id, sample_rate, radius, height, debug_mode );

}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    clientId += String(random(0xffff), HEX);
    String msg_connected_with_client_id =  clientId + msg_connected;
    String msg_subscribed_with_client_id = clientId + msg_subscribed;

    // Attempt to connect

    if (client.connect(clientId.c_str(), mqtt_username, mqtt_password)) {
      // Once connected, publish an announcement...
      client.publish(connect_topic, msg_connected_with_client_id.c_str() );
      // ... and resubscribe
      if (client.subscribe(configure_topic)) {
        client.publish(connect_topic, msg_subscribed_with_client_id.c_str());
      } else {
        client.publish(connect_topic, "ERROR");
      }

    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  pinMode(BUILTIN_LED, OUTPUT);     // Initialize the BUILTIN_LED pin as an output
  Serial.begin(115200);
  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  //Distance sensor_id
  pinMode(trigger, OUTPUT);
  pinMode(echo, INPUT);
}





void publishToBroker(int distance) {
  int waterlevel = height - distance;
  int liter = (Pi * pow(radius, 2) * (waterlevel)) / 1000;

  Serial.print("Publish message: ");
  Serial.println(liter);
  DynamicJsonDocument doc2;

  String input = "{\"name\":\"\",\"distance\":\"\"}";
  deserializeJson(doc2, input);
  JsonObject obj = doc2.as<JsonObject>();
  obj[String("name")] = "Ultrasound";
  obj[String("distance")] = distance;

  String output;
  serializeJson(doc2, output);
  Serial.println(output);

  char data[180];
  output.toCharArray(data, (output.length() + 1));
  client.publish(value_topic, data);
}

void publishInfoLogging(String info ) {
  if (debug_mode) {
    Serial.print("Publish Info message");
    DynamicJsonDocument doc2;

    String input = "{\"info\":\"\"}";
    deserializeJson(doc2, input);
    JsonObject obj = doc2.as<JsonObject>();
    obj[String("info")] = info;

    String output;
    serializeJson(doc2, output);
    Serial.println(output);

    char data[180];
    output.toCharArray(data, (output.length() + 1));
    client.publish(info_topic, data);
  }
}

void publishInfoToBroker(String sensor_id_received,  int interval_received, int radius_received, int height_received, boolean debug_mode ) {
  Serial.print("Publish Info message");
  DynamicJsonDocument doc2;

  String input = "{\"id\":\"\",\"i\":\"\",\"r\":\"\",\"h\":\"\",\"dm\":\"\",\"nr\":\"\"}";
  deserializeJson(doc2, input);
  JsonObject obj = doc2.as<JsonObject>();
  obj[String("id")] = sensor_id_received;
  obj[String("i")] = interval_received;
  obj[String("r")] = radius_received;
  obj[String("h")] = height_received;
  obj[String("dm")] = debug_mode;
  obj[String("nr")] = num_readings;
  String output;
  serializeJson(doc2, output);
  Serial.println(output);

  char data[180];
  output.toCharArray(data, (output.length() + 1));
  client.publish(info_topic, data);
}



