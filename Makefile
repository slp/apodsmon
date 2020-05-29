CC=gcc
CFLAGS=-I/usr/include/dbus-1.0 -I/usr/lib64/dbus-1.0/include -I/usr/include/glib-2.0 -I/usr/lib64/glib-2.0/include -pthread -I/usr/include/libmount -I/usr/include/blkid

LIBS=-ldbus-1 -lgio-2.0 -lgobject-2.0 -lglib-2.0

apodsmon: main.o object.o client.o watch.o polkit.o mainloop.o
	$(CC) -o $@ $^ $(CFLAGS) $(LIBS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	@rm -f apodsmon *.o
