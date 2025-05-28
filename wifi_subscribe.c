#include <MQTTClient.h>
#include <stdio.h>
#include <unistd.h>
#include "config_loader.h"

#define CONFIG_FILE "/home/vvdn/RAVI/MQTT/config.json"

/**
 * Callback function called when a new MQTT message arrives.
 */
int msgarrvd(void *context, char *topicName, int topicLen, MQTTClient_message *message) 
{
	printf("New WiFi Scan Data Received:\n%.*s\n\n", message->payloadlen, (char *)message->payload);
	MQTTClient_freeMessage(&message);
	MQTTClient_free(topicName);
	return 1;
}

/**
 * Callback function called when the MQTT connection is lost.
 */
void connlost(void *context, char *cause) 
{
	printf("Connection lost: %s\n", cause);
}

int main() 
{
	// Load configuration JSON from file
	if (load_config(CONFIG_FILE) != 0) 
	{
		fprintf(stderr, "Error: Failed to load config file %s\n", CONFIG_FILE);
		return 1;
	}

	MQTTClient client;
	MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

	// Extract parameters from loaded JSON config
	const char *address = json_object_get_string(json_object_object_get(config_json, "mqtt_address"));
	const char *client_id = json_object_get_string(json_object_object_get(config_json, "client_id_subscriber"));
	const char *topic = json_object_get_string(json_object_object_get(config_json, "topic"));
	int qos = json_object_get_int(json_object_object_get(config_json, "qos"));

	if (!address || !client_id || !topic) 
	{
		fprintf(stderr, "Error: Missing required MQTT configuration parameters\n");
		json_object_put(config_json);
		return 1;
	}

	// Create MQTT client
	int rc = MQTTClient_create(&client, address, client_id, MQTTCLIENT_PERSISTENCE_NONE, NULL);
	if (rc != MQTTCLIENT_SUCCESS) 
	{
		fprintf(stderr, "Error: Failed to create MQTT client, return code %d\n", rc);
		json_object_put(config_json);
		return 1;
	}

	conn_opts.keepAliveInterval = 20;
	conn_opts.cleansession = 1;

	// Set callback functions for connection loss and message arrival
	MQTTClient_setCallbacks(client, NULL, connlost, msgarrvd, NULL);

	// Connect to MQTT broker
	rc = MQTTClient_connect(client, &conn_opts);
	if (rc != MQTTCLIENT_SUCCESS) 
	{
		fprintf(stderr, "Error: Failed to connect to MQTT broker, return code %d\n", rc);
		MQTTClient_destroy(&client);
		json_object_put(config_json);
		return 1;
	}

	printf("Connected to MQTT broker.\nSubscribing to topic: %s\n", topic);

	// Subscribe to topic
	rc = MQTTClient_subscribe(client, topic, qos);
	if (rc != MQTTCLIENT_SUCCESS) 
	{
		fprintf(stderr, "Error: Failed to subscribe to topic %s, return code %d\n", topic, rc);
		MQTTClient_disconnect(client, 10000);
		MQTTClient_destroy(&client);
		json_object_put(config_json);
		return 1;
	}

	// Loop forever to keep receiving messages
	while (1) 
	{
		sleep(1);
	}

	// Cleanup code (never reached due to infinite loop)
	MQTTClient_disconnect(client, 10000);
	MQTTClient_destroy(&client);
	json_object_put(config_json);

	return 0;
}

