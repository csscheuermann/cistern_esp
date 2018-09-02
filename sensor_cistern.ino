
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

const char* ssid = "XXX";
const char* password = "XXX";
const char* mqtt_server = "192.168.178.86";
const char* configure_topic = "init/0001";
const char* error_topic = "sensor/error";
const char* connect_topic = "sensor/connect";
const char* info_topic = "sensor/info";

const char* mqtt_username = "mosquitto";
const char* mqtt_password = "mosquitto";

// Create a random client ID
String clientId = "SmartObject-Zysterne-";


int height = 180;
int radius = 109;
bool debug_mode = true;


int num_readings = 5;

long total = 0;                  // the running total
long average = 0;                // the average

//Messages
const char* error_sensor = "| Something is wrong with my Sensor. I am measuring more than maximum height or less than minimum. I need help.";
String msg_connected = " | I am connected.";
String msg_subscribed = "| I am subscribed to configure topic.";
String msg_measuring = "| I am measuring.";
String msg_nothing_changed = "| Nothing changed, but I will still store it in database.";

//Subscribe Stuff
String sensor;
//Default SampleRate (4 times a day 1296000)
int sample_rate = 20000;

//Distance Stuff
float Pi = 3.14159;

int trigger = 0;
int echo = 2;
long duration = 0;
float distance = 0;
int previous_distance = 0;



WiFiClient espClient;
PubSubClient client(espClient);
StaticJsonDocument<200> doc;


void loop() {
  measureDistance();
  checkAndPublish();
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

  String sensor_received = root["id"];
  sensor = sensor_received;

  int interval_received = root["i"];
  sample_rate = interval_received;

  int radius_received = root["r"];
  radius = radius_received;

  int height_received = root["h"];
  height = height_received;

  int num_reads_received = root["n_r"];
  num_readings = num_reads_received;

  bool debug_mode_received = root["dm"];
  debug_mode = debug_mode_received;
 
  publishInfoToBroker(sensor,sample_rate,radius,height,debug_mode );

}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    clientId += String(random(0xffff), HEX);
    String msg_connected_with_client_id =  clientId+msg_connected; 
    String msg_subscribed_with_client_id = clientId+msg_subscribed;    
  
    // Attempt to connect

    if (client.connect(clientId.c_str(),mqtt_username,mqtt_password)) {
         // Once connected, publish an announcement...
      client.publish(connect_topic, msg_connected_with_client_id.c_str() );
      // ... and resubscribe
      if (client.subscribe(configure_topic)){
         client.publish(connect_topic, msg_subscribed_with_client_id.c_str());
      }else{
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
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);

  //Distance Sensor
  pinMode(trigger, OUTPUT);
  pinMode(echo, INPUT);
}

void measureDistance(){
  float maximum = 0;

  for (int thisReading = 0; thisReading < num_readings; thisReading++) {
   
    // read from the sensor:
    digitalWrite(trigger, LOW);
    delay(5);
    digitalWrite(trigger, HIGH);
    delay(10);
    digitalWrite(trigger, LOW);
    float measurement = pulseIn(echo, HIGH);
    
    maximum = _max(maximum, measurement);
     
    float waterlevel = height - (maximum / 2) / 29.1;
    String msg_measuring_with_client_id = clientId+msg_measuring +" "+thisReading+"/"+num_readings +"("+waterlevel+" Waterlevel)";  
    publishInfoLogging(msg_measuring_with_client_id);
    
    delay(sample_rate);
  }
  distance =(maximum / 2) / 29.1;
 
}
  


void checkAndPublish(){
   if (!client.connected()) {
      reconnect();
   }   
   client.loop();
  if (distance > height || distance < 0 ){
      String error_sensor_with_client_id = clientId+error_sensor;
      client.publish(error_topic,error_sensor_with_client_id.c_str());
  }else if(previous_distance == distance )   {
      String msg_nothing_changed_with_client_id = clientId+msg_nothing_changed;
      client.publish(error_topic,msg_nothing_changed_with_client_id.c_str());
      int waterlevel = height - distance;
     int liter = (Pi * pow(radius, 2) *(waterlevel))/1000;
     publishToBroker(liter, waterlevel);
  }else{ 
     int waterlevel = height - distance;
     int liter = (Pi * pow(radius, 2) *(waterlevel))/1000;
     publishToBroker(liter, waterlevel);
  }
  
  previous_distance = distance;
   
}

void publishToBroker(int liter, int waterlevel){
    if (!client.connected()) {
      reconnect();
   }   
   client.loop();
    Serial.print("Publish message: ");
    Serial.println(liter);
    DynamicJsonDocument doc2;
  
    String input = "{\"sensorId\":\"\",\"value\":\"\",\"unit\":\"\",\"waterlevel\":\"\"}";
    deserializeJson(doc2, input);
    JsonObject obj = doc2.as<JsonObject>();
    obj[String("sensorId")] = sensor;
    obj[String("value")] = liter;
    obj[String("unit")] = "Liter";
    obj[String("waterlevel")] = waterlevel;

    String output;
    serializeJson(doc2, output);
    Serial.println(output);

    char data[180];
    output.toCharArray(data, (output.length() + 1));
    client.publish("sensorvalue",data);
}

void publishInfoLogging(String info ){
 if (!client.connected()) {
      reconnect();
   }   
   client.loop();
if(debug_mode){
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
    client.publish(info_topic,data);
}
}

void publishInfoToBroker(String sensor_received,  int interval_received,int radius_received,int height_received, boolean debug_mode ){
 if (!client.connected()) {
      reconnect();
   }   
   client.loop();
    Serial.print("Publish Info message");
    DynamicJsonDocument doc2;

    String input = "{\"sensorId\":\"\",\"interval\":\"\",\"radius\":\"\",\"height\":\"\",\"debug_mode\":\"\",\"num_reads\":\"\"}";
    deserializeJson(doc2, input);
    JsonObject obj = doc2.as<JsonObject>();
    obj[String("sensorId")] = sensor_received;
    obj[String("interval")] = interval_received;
    obj[String("radius")] = radius_received;
    obj[String("height")] = height_received;
    obj[String("debug_mode")] = debug_mode;
    obj[String("num_reads")] = num_readings;
    String output;
    serializeJson(doc2, output);
    Serial.println(output);

  
    char data[180];
    output.toCharArray(data, (output.length() + 1));
    client.publish(info_topic,data);
}



