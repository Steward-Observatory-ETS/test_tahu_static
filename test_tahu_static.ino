/*
Copyright (c) 2020
Steward Observatory Engineering & Technical Services, University of Arizona

This program and the accompanying materials are made available under the
terms of the Eclipse Public License 2.0 which is available at
http://www.eclipse.org/legal/epl-2.0.

This is a test program designed to respond to commands to turn on/off the LED
on a Teensy 4.1 using Sparkplug B encoded MQTT messages.

The Teensy connects to an MQTT broker at the IP address specified by "mqtt_server"
It then subscribes to the topic "spBv1.0/dev/DCMD/Teensy1/led", with a single
boolean value metric. The Teensy sets the LED value based on the commanded boolean
value attached to the above topic message.

Each time the LED changes value, the Teensy publishes a message to the topic
"spBv1.0/dev/DDATA/Teensy1/led" to inform listeners of the state change.

This test app uses NTP to get and maintain a local system time for
providing semi-accurate time stamps in the Sparkplug B messages.

Additionally, there is a diagnostic HTTP server which is used to verify the
Teensy is alive and working.

Compiled/tested with Arduino 1.8.13

Michael Sibayan 2020
Steward Observatory
*/
#include <SPI.h>
#include <NativeEthernet.h> // Teensy1 requires NativeEthernet to use onboard NIC
#include <NTPClient_Generic.h> // for NTP time
#include <PubSubClient.h> // pub&sub MQTT messages
#include "tahu.h" // encode/decode Sparkplug B messages
//#include "tahu_b.pb.h"
//#include "pb_encode.h"
//#include "pb_decode.h"

#define TIME_ZONE_OFFSET_HRS            (-4) // MST?
//#define MQTT_ID                      "Teensy1"
#define METRIC_DATA_TYPE_STRING 12
//#define PROPERTY_DATA_TYPE_UINT64 8

// Enter a MAC address and IP address for your controller below.
// The IP address will be dependent on your local network:
byte mac[] = {
  0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED
};
IPAddress ip(192, 168, 249, 99);
IPAddress myDns(192, 168, 249, 1);
IPAddress gateway(192, 168, 249, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress mqtt_server(192, 168, 249, 98);

// alias map used by example program, probably need to generate our own
enum alias_map {
	Next_Server = 0,
	Rebirth = 1,
	Reboot = 2,
	Dataset = 3,
	Node_Metric0 = 4,
	Node_Metric1 = 5,
	Node_Metric2 = 6,
	Device_Metric0 = 7,
	Device_Metric1 = 8,
	Device_Metric2 = 9,
	Device_Metric3 = 10,
	My_Custom_Motor = 11
};

EthernetUDP ntpUDP;
NTPClient timeClient(ntpUDP);
EthernetServer server(80);
EthernetClient enetClient;
PubSubClient mmqtClient(enetClient);

uint8_t buf[1024];
char logBuf[1024];

int message_length;
const int ledPin = 13;
char pubTopic[50];
char pubData[100];

String mqtt_msg;

//char *metric_name = "led_val";
const char* MQTT_TOPIC_TIME = "spBv1.0/dev/DDATA/Teensy1/led";
const char* MQTT_TOPIC_LED = "spBv1.0/dev/DCMD/Teensy1/led";
char pb_as_hex[2560];
char rx_as_hex[2560];
bool led_val;
bool old_led_val = 0;
int rec = 0;

// ---- TAHU ----
org_eclipse_tahu_protobuf_Payload payload;
org_eclipse_tahu_protobuf_Payload_Metric metrics; // for this test we only have one metric
org_eclipse_tahu_protobuf_Payload payload_in = org_eclipse_tahu_protobuf_Payload_init_zero;
org_eclipse_tahu_protobuf_Payload_Metric metrics_in;
size_t buffer_length = 1024;
uint8_t binary_buffer[256];
uint8_t binary_buffer_in[256];

// ============== MQTT Subscription callback ===================================
void callback(char* topic, byte* payload, unsigned int length){
  // convert the message to hex, so we can se it in HTTP diagnostic
  char *ptr;
  unsigned int i;
  ptr=rx_as_hex;
  for(i=0; i<length; i++){
    if(payload[i] > 31 && payload[i] < 127){
      ptr += sprintf(ptr, "%c", payload[i]);
    }
    else{
      ptr += sprintf(ptr, " 0x%x ", payload[i]);
    }
  }

  rec++;
  if(!decode_payload(&payload_in, payload, length)){
    mqtt_msg += "Failed to decode payload " + String(rec);
  }
  else{
    mqtt_msg = "Packet# " + String(rec) + "<br>";
    mqtt_msg += "metric size: " + String(payload_in.metrics_count) + "<br>";
    if(String(topic).compareTo(MQTT_TOPIC_LED) == 0){
      if(payload_in.metrics_count == 1){
        if(payload_in.metrics[0].value.boolean_value == true){
          pinMode(ledPin, OUTPUT);
          digitalWrite(ledPin, HIGH);   // set the LED on
          led_val = true;
          mqtt_msg += String(topic) + " = TRUE ";
        }
        else{
          digitalWrite(ledPin, LOW);
          led_val = false;
          mqtt_msg += String(topic) + " = FALSE";
        }
      }
    }else{
      mqtt_msg += String(topic);
    }
  }
  free_payload(&payload_in); // make sure to free, since it uses malloc
  // note: calling decode_payload will keep adding metrics to the decoded
  // payload if we do not free_payload when we're done.
  payload_in = org_eclipse_tahu_protobuf_Payload_init_zero;
  // for some reason i have to assign payload_in here, or the system crashes...
}

// ============== Sparkplug B publish birth certs ==============================
void publish_births(){
  char *birth_topic = "spBv1.0/dev/DBIRTH/Teensy1/led";
  char dbirth_metric_zero_value[] = "hello device";
  org_eclipse_tahu_protobuf_Payload dbirth_payload;
  get_next_payload(&dbirth_payload);
  set_tahu_time(timeClient.getUTCEpochTime());
  add_simple_metric(&dbirth_payload, birth_topic, true, Device_Metric0, METRIC_DATA_TYPE_STRING, false, false, false, &dbirth_metric_zero_value, sizeof(dbirth_metric_zero_value));

  uint8_t *buf_ptr = (uint8_t*)binary_buffer;
  message_length = encode_payload(&buf_ptr, 256, &dbirth_payload);
  mmqtClient.publish(birth_topic, binary_buffer, message_length, 0);
}

// ============== Setup all objects ============================================
void setup() {
  led_val = 0;

  // --------- TAHU -----------------
  // create a payload and fill in the struct with appropriate values
  get_next_payload(&payload);
  // don't use add_simple_metric, it uses malloc...
  //add_simple_metric(&payload, "led_val", true, Device_Metric0, METRIC_DATA_TYPE_BOOLEAN, false, false, false, &led_val, sizeof(led_val));
  payload.metrics = &metrics;
  payload.metrics_count = 1;
  metrics.name = "led_val"; //metric_name;
  metrics.has_alias = true;
  metrics.alias = Device_Metric0;
  metrics.has_timestamp = true;
  metrics.timestamp = 0;
  metrics.has_datatype = true;
  metrics.datatype = METRIC_DATA_TYPE_BOOLEAN;
  metrics.has_is_historical = false;
  metrics.has_is_transient = false;
  metrics.has_is_null = false;
  metrics.has_metadata = false;
  metrics.has_properties = false;
  metrics.value.string_value = NULL;
  metrics.which_value = org_eclipse_tahu_protobuf_Payload_Metric_boolean_value_tag;
  metrics.value.boolean_value = false;
  // ------- END TAHU --------------

  sprintf(pb_as_hex, "no message");
  message_length = 0;

  // "begin" ethernet
  Ethernet.begin(mac, ip,myDns, gateway, subnet );
  mqtt_msg = "none = none";

  // Check for Ethernet hardware present
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
      pinMode(ledPin, OUTPUT);
      digitalWrite(ledPin, HIGH);   // set the LED on
      while(1){
          digitalWrite(ledPin, HIGH);
          delay(100);
          digitalWrite(ledPin, LOW);
          delay(100);
      }
  }
  if (Ethernet.linkStatus() == LinkOFF) {
    pinMode(ledPin, OUTPUT);
    digitalWrite(ledPin, HIGH);   // set the LED on
  }

  // ---- NTP time client --------------
  timeClient.begin();
  timeClient.setTimeOffset(3600 * TIME_ZONE_OFFSET_HRS);
  // default 60000 => 60s. Set to once per hour
  timeClient.setUpdateInterval(SECS_IN_HR);

  // ----- Setup MqttClient ----------------
  mmqtClient.setServer(mqtt_server, 1883);
  mmqtClient.setCallback(callback);

  // start the server (used for diagnostic HTTP)
  server.begin();
}

//bool doShow = false;
//bool is_con = false;
// ============== Main loop ====================================================
void loop() {

  char *ptr;
  int i;
  bool updated = timeClient.update();

  // ---------------------------------- HTTP Diagnostic ------------------------
  // listen for incoming clients
  EthernetClient client = server.available();
  if (client) {
    //Serial.println("new client");
    // an http request ends with a blank line
    boolean currentLineIsBlank = true;
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        //Serial.write(c);
        //// if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if (c == '\n' && currentLineIsBlank) {
          // send a standard http response header
          client.println("HTTP/1.1 200 OK");
          client.println("Content-Type: text/html");
          client.println("Connection: close");  // the connection will be closed after completion of the response
          client.println("Refresh: 5");  // refresh the page automatically every 5 sec
          client.println();
          client.println("<!DOCTYPE HTML>");
          client.println("<html>");
          client.println("UTC : " + timeClient.getFormattedUTCDateTime() + "<br>");
          client.println("timestamp: " + String(timeClient.getUTCEpochTime()) + "<br><hr>");

          if(mmqtClient.connected()){
            client.println("Connected to MQTT<br>");
            /*client.println("has_timestamp: " + String(inbound_payload.has_timestamp) + "<br>");
            client.println("timestamp: " + String((uint32_t)(inbound_payload.timestamp & 0xFFFF)) + "<br>");
            client.println("has_seq: " + String(inbound_payload.has_seq) + "<br>");
            client.println("Sequence: " + String(((uint32_t)inbound_payload.seq & 0xFFFF))  + "<br>");*/
            //client.println("metrics_count: " + String(inbound_payload.metrics_count) + "<br>");

            //client.println("metrics[0].name: " + String(payload.metrics[0].name) + "<br>");
            client.println("<br>TX: " + String(pb_as_hex) + "<br>");
            client.println("RX: " + String(rx_as_hex) + "<br>");
          }
          else
           client.println("Not Connected to MQTT<br>");

          client.println("<hr>" + mqtt_msg);
          client.println("</html>");
          break;
        }
        if (c == '\n') {
          // you're starting a new line
          currentLineIsBlank = true;
        } else if (c != '\r') {
          // you've gotten a character on the current line
          currentLineIsBlank = false;
        }
      }
    }
    // give the web browser time to receive the data
    delay(1);
    client.stop();
  }

  // ----- MQTT related stuff --------------------------------------------------

  if(!mmqtClient.connected()){
    if(mmqtClient.connect("Teensy1")){
      //publish_births();
      mmqtClient.subscribe(MQTT_TOPIC_LED);
    }else{
      mqtt_msg = "failed to connect";
    }
  }
  if(mmqtClient.connected()){
    //-------------------------------------------------------------------------
    // Pubish the new LED value to the MQTT broker IF the LED value has changed
    //
    if(led_val != old_led_val){
      old_led_val = led_val;
      //timeClient.getFormattedUTCDateTime().toCharArray(pubData, 100);
      set_tahu_time(timeClient.getUTCEpochTime()); // helper to set time in TAHU.c

      // Fill in the payload data
      payload.timestamp = timeClient.getUTCEpochTime();
      payload.metrics[0].timestamp = timeClient.getUTCEpochTime();
      payload.metrics[0].value.boolean_value = led_val;
      payload.seq++;
      uint8_t *buf_ptr = (uint8_t*)binary_buffer;

      // Encode the payload
      message_length = encode_payload(&buf_ptr, 256, &payload);

      // convert the message to hex, so we can se it in HTTP diagnostic
      ptr=pb_as_hex;
      for(i=0; i<message_length; i++){
        if(binary_buffer[i] > 31 && binary_buffer[i] < 127){
          ptr += sprintf(ptr, "%c", binary_buffer[i]);
        }
        else{
          ptr += sprintf(ptr, " 0x%x ", binary_buffer[i]);
        }
      }


      // Publish Sparkplug B encoded MQTT message to broker
      mmqtClient.publish(MQTT_TOPIC_TIME, binary_buffer, message_length, 0);

        //doShow = true;
    }

    mmqtClient.loop();
  }
}
