CC = gcc
CFLAGS = -I/usr/include/libnl3
LIBS = -lnl-3 -lnl-genl-3 -ljson-c -lpaho-mqtt3c

SRC = config_loader.c

all: wifi_publisher wifi_subscribe

wifi_publisher: wifi_publisher.c $(SRC)
	$(CC) wifi_publisher.c $(SRC) -o wifi_publisher $(CFLAGS) $(LIBS)

wifi_subscribe: wifi_subscribe.c $(SRC)
	$(CC) wifi_subscribe.c $(SRC) -o wifi_subscribe $(CFLAGS) $(LIBS)

clean:
	rm -f wifi_publisher wifi_subscribe

