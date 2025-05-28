#include <stdio.h>
#include <stdlib.h>
#include <json-c/json.h>
#include "config_loader.h"


json_object *config_json = NULL;

int load_config(const char *filename) 
{

    FILE *fp = fopen(filename, "r");
    if (!fp) 
    {
        perror("Failed to open config file");
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);

    rewind(fp);

    char *buffer = malloc(filesize + 1);
    if (!buffer) 
    {
        fclose(fp);
        fprintf(stderr, "Memory allocation failed\n");
        return -1;
    }

    fread(buffer, 1, filesize, fp);
    buffer[filesize] = '\0';
    fclose(fp);

    config_json = json_tokener_parse(buffer);
    free(buffer);

    if (!config_json) 
    {
        fprintf(stderr, "Failed to parse JSON config\n");
        return -1;
    }
    return 0;
}

