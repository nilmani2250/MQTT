#ifndef CONFIG_LOADER_H
#define CONFIG_LOADER_H

#include <json-c/json.h>

extern json_object *config_json;

int load_config(const char *filename);

#endif

