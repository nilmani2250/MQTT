#include <netlink/netlink.h>
#include <netlink/genl/genl.h>
#include <netlink/genl/ctrl.h>
#include <linux/nl80211.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <unistd.h>
#include <net/if.h>
#include <errno.h>
#include <MQTTClient.h>
#include <json-c/json.h>
#include "config_loader.h"

#define CONFIG_FILE "config.json"
#define NL80211_DRIVER "nl80211"

#define MQTT_CLIENT_ID_KEY "client_id_publisher"
#define MQTT_ADDRESS_KEY "mqtt_address"
#define MQTT_QOS_KEY "qos"
#define MQTT_TOPIC_KEY "topic"
#define MQTT_ACK_TOPIC_KEY "ack_topic"
#define MQTT_TIMEOUT_KEY "timeout"

MQTTClient client;

/**
 * Callback function for MQTT ACK message arrival.
 */
int ack_arrived(void *context, char *topicName, int topicLen, MQTTClient_message *message) 
{
	printf("\n\u2705 ACK Received: %.*s\n", message->payloadlen, (char *)message->payload);
	MQTTClient_freeMessage(&message);
	MQTTClient_free(topicName);
	return 1;
}

/**
 * Convert frequency in MHz to Wi-Fi channel number.
 * Returns channel number or -1 if frequency is invalid.
 */
int freq_to_channel(int freq) 
{
	if (freq >= 2412 && freq <= 2472)
		return (freq - 2407) / 5;
	if (freq == 2484)
		return 14;
	if (freq >= 5180 && freq <= 5825)
		return (freq - 5000) / 5;
	return -1;
}

/**
 * Callback to parse and publish Wi-Fi scan info as JSON via MQTT.
 */
int print_wifi_info(struct nl_msg *msg, void *arg) 
{
	struct genlmsghdr *header = nlmsg_data(nlmsg_hdr(msg));
	struct nlattr *attrs[NL80211_ATTR_MAX + 1];
	struct nlattr *bss[NL80211_BSS_MAX + 1];

	nla_parse(attrs, NL80211_ATTR_MAX, genlmsg_attrdata(header, 0),
			genlmsg_attrlen(header, 0), NULL);

	if (!attrs[NL80211_ATTR_BSS])
		return NL_SKIP;

	nla_parse_nested(bss, NL80211_BSS_MAX, attrs[NL80211_ATTR_BSS], NULL);

	json_object *wifi_info = json_object_new_object();

	if (bss[NL80211_BSS_BSSID]) 
	{
		unsigned char *mac = nla_data(bss[NL80211_BSS_BSSID]);
		char mac_str[18];
		snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x",
				mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		json_object_object_add(wifi_info, "mac", json_object_new_string(mac_str));
	}

	if (bss[NL80211_BSS_INFORMATION_ELEMENTS]) 
	{
		unsigned char *ie = nla_data(bss[NL80211_BSS_INFORMATION_ELEMENTS]);
		int len = nla_len(bss[NL80211_BSS_INFORMATION_ELEMENTS]);
		for (int i = 0; i + 1 < len;) 
		{
			if (ie[i] == 0) 
			{
				int ssid_len = ie[i + 1];
				json_object_object_add(wifi_info, "ssid",
						json_object_new_string_len((const char *)&ie[i + 2], ssid_len));
				break;
			}
			i += ie[i + 1] + 2;
		}
	}

	if (bss[NL80211_BSS_SIGNAL_MBM]) 
	{
		int signal = nla_get_u32(bss[NL80211_BSS_SIGNAL_MBM]);
		double dbm = signal / 100.0;
		json_object_object_add(wifi_info, "signal_dbm", json_object_new_double(dbm));
	}

	if (bss[NL80211_BSS_FREQUENCY]) 
	{
		int freq = nla_get_u32(bss[NL80211_BSS_FREQUENCY]);
		int channel = freq_to_channel(freq);
		double freq_ghz = freq / 1000.0;
		json_object_object_add(wifi_info, "frequency_ghz", json_object_new_double(freq_ghz));
		json_object_object_add(wifi_info, "channel", json_object_new_int(channel));
	}

	const char *json_str = json_object_to_json_string(wifi_info);

	MQTTClient_message pubmsg = MQTTClient_message_initializer;
	pubmsg.payload = (void *)json_str;
	pubmsg.payloadlen = strlen(json_str);
	pubmsg.qos = json_object_get_int(json_object_object_get(config_json, MQTT_QOS_KEY));
	pubmsg.retained = 0;

	MQTTClient_deliveryToken token;
	MQTTClient_publishMessage(client,
			json_object_get_string(json_object_object_get(config_json, MQTT_TOPIC_KEY)),
			&pubmsg, &token);

	MQTTClient_waitForCompletion(client, token,
			json_object_get_int(json_object_object_get(config_json, MQTT_TIMEOUT_KEY)));

	json_object_put(wifi_info);

	return NL_OK;
}

/**
 * Retrieves all wireless interfaces.
 * Returns dynamically allocated array of strings, sets count.
 */
char **get_all_wireless_interfaces(int *count) 
{
	DIR *dp = opendir("/sys/class/net");
	struct dirent *entry;
	char **interfaces = NULL;
	int found = 0;

	if (!dp) 
	{
		perror("opendir");
		return NULL;
	}

	while ((entry = readdir(dp))) 
	{
		const char *name = entry->d_name;
		if (name[0] == '.')
			continue;

		char path[512];
		snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", name);

		if (access(path, F_OK) == 0) 
		{
			char **tmp = realloc(interfaces, sizeof(char *) * (found + 1));
			if (!tmp) 
			{
				perror("realloc");
				// free previously allocated interfaces
				for (int j = 0; j < found; j++) 
				{
					free(interfaces[j]);
				}
				free(interfaces);
				closedir(dp);
				return NULL;
			}
			interfaces = tmp;
			interfaces[found] = strdup(name);
			if (!interfaces[found]) 
			{
				perror("strdup");
				// free previously allocated interfaces
				for (int j = 0; j < found; j++) 
				{
					free(interfaces[j]);
				}
				free(interfaces);
				closedir(dp);
				return NULL;
			}
			found++;
		}
	}

	closedir(dp);
	*count = found;
	return interfaces;
}

/**
 * Performs Wi-Fi scan on a single interface.
 * Returns 0 on success, -1 on failure.
 */
int scan_interface(const char *iface) 
{
	int ifindex = if_nametoindex(iface);
	if (ifindex == 0) 
	{
		fprintf(stderr, "Error: Could not get index for interface %s\n", iface);
		return -1;
	}

	struct nl_sock *sock = nl_socket_alloc();
	if (!sock) 
	{
		fprintf(stderr, "Error: Failed to allocate netlink socket for interface %s\n", iface);
		return -1;
	}

	if (genl_connect(sock) != 0) 
	{
		fprintf(stderr, "Error: Failed to connect generic netlink socket for interface %s\n", iface);
		nl_socket_free(sock);
		return -1;
	}

	int driver_id = genl_ctrl_resolve(sock, NL80211_DRIVER);
	if (driver_id < 0) 
	{
		fprintf(stderr, "Error: nl80211 driver not found for interface %s\n", iface);
		nl_socket_free(sock);
		return -1;
	}

	struct nl_msg *msg = nlmsg_alloc();
	if (!msg) 
	{
		fprintf(stderr, "Error: Failed to allocate netlink message for interface %s\n", iface);
		nl_socket_free(sock);
		return -1;
	}

	genlmsg_put(msg, 0, 0, driver_id, 0, NLM_F_DUMP, NL80211_CMD_GET_SCAN, 0);
	nla_put_u32(msg, NL80211_ATTR_IFINDEX, ifindex);

	struct nl_cb *cb = nl_cb_alloc(NL_CB_DEFAULT);
	if (!cb) 
	{
		fprintf(stderr, "Error: Failed to allocate netlink callbacks for interface %s\n", iface);
		nlmsg_free(msg);
		nl_socket_free(sock);
		return -1;
	}

	nl_cb_set(cb, NL_CB_VALID, NL_CB_CUSTOM, print_wifi_info, NULL);

	if (nl_send_auto(sock, msg) < 0) 
	{
		fprintf(stderr, "Error: Failed to send netlink message for interface %s\n", iface);
		nl_cb_put(cb);
		nlmsg_free(msg);
		nl_socket_free(sock);
		return -1;
	}

	nl_recvmsgs(sock, cb);

	nl_cb_put(cb);
	nlmsg_free(msg);
	nl_socket_free(sock);

	return 0;
}

int main() 
{
	// Load config JSON
	if (load_config(CONFIG_FILE) != 0) 
	{
		fprintf(stderr, "Error: Failed to load config from %s. Exiting.\n", CONFIG_FILE);
		return 1;
	}

	printf("Config loaded from %s\n", CONFIG_FILE);

	MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

	client = NULL;
	MQTTClient_create(&client,
			json_object_get_string(json_object_object_get(config_json, MQTT_ADDRESS_KEY)),
			json_object_get_string(json_object_object_get(config_json, MQTT_CLIENT_ID_KEY)),
			MQTTCLIENT_PERSISTENCE_NONE, NULL);

	conn_opts.keepAliveInterval = 20;
	conn_opts.cleansession = 1;

	MQTTClient_setCallbacks(client, NULL, NULL, ack_arrived, NULL);

	if (MQTTClient_connect(client, &conn_opts) != MQTTCLIENT_SUCCESS) 
	{
		fprintf(stderr, "Error: Failed to connect to MQTT broker\n");
		return 1;
	}

	printf("Connected to MQTT broker\n");

	MQTTClient_subscribe(client,
			json_object_get_string(json_object_object_get(config_json, MQTT_ACK_TOPIC_KEY)),
			json_object_get_int(json_object_object_get(config_json, MQTT_QOS_KEY)));

	int count = 0;
	char **interfaces = get_all_wireless_interfaces(&count);
	if (!interfaces || count == 0) {
		fprintf(stderr, "Error: No wireless interfaces found\n");
		MQTTClient_disconnect(client,
				json_object_get_int(json_object_object_get(config_json, MQTT_TIMEOUT_KEY)));
		MQTTClient_destroy(&client);
		json_object_put(config_json);
		return 1;
	}

	printf("Found %d wireless interface(s)\n", count);

	for (int i = 0; i < count; i++) 
	{
		printf("Scanning interface %s\n", interfaces[i]);
		if (scan_interface(interfaces[i]) != 0) 
		{
			fprintf(stderr, "Warning: Scanning failed for interface %s\n", interfaces[i]);
		}
		free(interfaces[i]);
	}

	free(interfaces);

	MQTTClient_disconnect(client, json_object_get_int(json_object_object_get(config_json, MQTT_TIMEOUT_KEY)));
	MQTTClient_destroy(&client);
	json_object_put(config_json);

	printf("Program finished successfully.\n");
	return 0;
}

