/*
 *  Copyright (C) 2020  Sergio Lopez. All rights reserved.
 *  Copyright (C) 2012  Intel Corporation. All rights reserved.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 */

#define _GNU_SOURCE
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <glib.h>

#include "gdbus.h"

static FILE *OUTPUT;

static DBusConnection *dbus_conn;

struct adapter {
	GDBusProxy *proxy;
	GDBusProxy *ad_proxy;
	GList *devices;
};

static struct adapter *default_ctrl;
static GDBusProxy *default_dev;

#define	DISTANCE_VAL_INVALID	0x7FFF

static struct set_discovery_filter_args {
	char *transport;
	char *pattern;
	dbus_uint16_t rssi;
	dbus_int16_t pathloss;
	char **uuids;
	size_t uuids_len;
	dbus_bool_t duplicate;
	dbus_bool_t discoverable;
	bool set;
	bool active;
} filter = {
	.rssi = DISTANCE_VAL_INVALID,
	.pathloss = DISTANCE_VAL_INVALID,
	.set = false,
    .discoverable = false,
    .active = true,
};

static void start_discovery_reply(DBusMessage *message, void *user_data)
{
	dbus_bool_t enable = GPOINTER_TO_UINT(user_data);
	DBusError error;

	dbus_error_init(&error);

	if (dbus_set_error_from_message(&error, message) == TRUE) {
		printf("Failed to %s discovery: %s\n",
				enable == TRUE ? "start" : "stop", error.name);
		dbus_error_free(&error);
        exit(-1);
	}

	filter.active = enable;
	/* Leave the discovery running even on noninteractive mode */
}

static void clear_discovery_filter(DBusMessageIter *iter, void *user_data)
{
	DBusMessageIter dict;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
				DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
				DBUS_TYPE_STRING_AS_STRING
				DBUS_TYPE_VARIANT_AS_STRING
				DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &dict);

	dbus_message_iter_close_container(iter, &dict);
}

static void set_discovery_filter_setup(DBusMessageIter *iter, void *user_data)
{
	struct set_discovery_filter_args *args = user_data;
	DBusMessageIter dict;

	dbus_message_iter_open_container(iter, DBUS_TYPE_ARRAY,
				DBUS_DICT_ENTRY_BEGIN_CHAR_AS_STRING
				DBUS_TYPE_STRING_AS_STRING
				DBUS_TYPE_VARIANT_AS_STRING
				DBUS_DICT_ENTRY_END_CHAR_AS_STRING, &dict);

	g_dbus_dict_append_array(&dict, "UUIDs", DBUS_TYPE_STRING,
							&args->uuids,
							args->uuids_len);

	if (args->pathloss != DISTANCE_VAL_INVALID)
		g_dbus_dict_append_entry(&dict, "Pathloss", DBUS_TYPE_UINT16,
						&args->pathloss);

	if (args->rssi != DISTANCE_VAL_INVALID)
		g_dbus_dict_append_entry(&dict, "RSSI", DBUS_TYPE_INT16,
						&args->rssi);

	if (args->transport != NULL)
		g_dbus_dict_append_entry(&dict, "Transport", DBUS_TYPE_STRING,
						&args->transport);

	if (args->duplicate)
		g_dbus_dict_append_entry(&dict, "DuplicateData",
						DBUS_TYPE_BOOLEAN,
						&args->duplicate);

	if (args->discoverable)
		g_dbus_dict_append_entry(&dict, "Discoverable",
						DBUS_TYPE_BOOLEAN,
						&args->discoverable);

	if (args->pattern != NULL)
		g_dbus_dict_append_entry(&dict, "Pattern", DBUS_TYPE_STRING,
						&args->pattern);

	dbus_message_iter_close_container(iter, &dict);
}


static void set_discovery_filter_reply(DBusMessage *message, void *user_data)
{
	DBusError error;

	dbus_error_init(&error);
	if (dbus_set_error_from_message(&error, message) == TRUE) {
		printf("SetDiscoveryFilter failed: %s\n", error.name);
		dbus_error_free(&error);
        exit(-1);
	}

	filter.set = true;
}

static void set_discovery_filter(bool cleared)
{
	GDBusSetupFunction func;

	func = cleared ? clear_discovery_filter : set_discovery_filter_setup;

	if (g_dbus_proxy_method_call(default_ctrl->proxy, "SetDiscoveryFilter",
					func, set_discovery_filter_reply,
					&filter, NULL) == FALSE) {
		printf("Failed to set discovery filter\n");
        exit(-1);
	}

	filter.set = true;
}

static struct adapter *adapter_new(GDBusProxy *proxy)
{
	struct adapter *adapter = g_malloc0(sizeof(struct adapter));
    adapter->proxy = proxy;
	return adapter;
}

static void adapter_added(GDBusProxy *proxy)
{
	struct adapter *adapter;
    dbus_bool_t enable = TRUE;

    if (!default_ctrl) {
        adapter = adapter_new(proxy);
        default_ctrl = adapter;

        set_discovery_filter(false);

        if (g_dbus_proxy_method_call(default_ctrl->proxy, "StartDiscovery",
                                     NULL, start_discovery_reply,
                                     GUINT_TO_POINTER(enable), NULL) == FALSE) {
            printf("Failed to %s discovery\n",
                            enable == TRUE ? "start" : "stop");
            exit(-1);
        }
    }
}

static int valid_left;
static int valid_right;
static int valid_case;

static void print_fixed_iter(const char *label, const char *name,
                             DBusMessageIter *iter, bool found)
{
	dbus_bool_t *valbool;
	dbus_uint32_t *valu32;
	dbus_uint16_t *valu16;
	dbus_int16_t *vals16;
	unsigned char *byte;
	int len;

	switch (dbus_message_iter_get_arg_type(iter)) {
	case DBUS_TYPE_BOOLEAN:
		dbus_message_iter_get_fixed_array(iter, &valbool, &len);

		if (len <= 0)
			return;

        break;
	case DBUS_TYPE_UINT32:
		dbus_message_iter_get_fixed_array(iter, &valu32, &len);

		if (len <= 0)
			return;

		break;
	case DBUS_TYPE_UINT16:
		dbus_message_iter_get_fixed_array(iter, &valu16, &len);

		if (len <= 0)
			return;

		break;
	case DBUS_TYPE_INT16:
		dbus_message_iter_get_fixed_array(iter, &vals16, &len);

		if (len <= 0)
			return;

		break;
	case DBUS_TYPE_BYTE:
		dbus_message_iter_get_fixed_array(iter, &byte, &len);

		if (len <= 0)
			return;

        if (found) {
            int left, right, _case;
            left = (byte[6] & 0xf0) >> 4;
            right = (byte[6] & 0xf);
            _case = (byte[7] & 0x0f);

            if (left <= 10) {
                valid_left = left;
            }
            if (right <= 10) {
                valid_right = right;
            }
            if (_case <= 10) {
                valid_case = _case;
            }

            fprintf(OUTPUT, "L: %d ", valid_left * 10);
            fprintf(OUTPUT, "R: %d ", valid_right * 10);
            fprintf(OUTPUT, "C: %d\n", valid_case * 10);
            fflush(OUTPUT);
        }
		break;
	default:
		return;
	};
}

static bool print_iter(const char *label, const char *name,
                       DBusMessageIter *iter, bool found)
{
	dbus_bool_t valbool;
	dbus_uint32_t valu32;
	dbus_uint16_t valu16;
	dbus_int16_t vals16;
    unsigned char byte;
	const char *valstr;
	DBusMessageIter subiter;
    char *entry;

	if (iter == NULL) {
		return false;
	}

	switch (dbus_message_iter_get_arg_type(iter)) {
	case DBUS_TYPE_INVALID:
		break;
	case DBUS_TYPE_STRING:
	case DBUS_TYPE_OBJECT_PATH:
		dbus_message_iter_get_basic(iter, &valstr);
		break;
	case DBUS_TYPE_BOOLEAN:
		dbus_message_iter_get_basic(iter, &valbool);
		break;
	case DBUS_TYPE_UINT32:
		dbus_message_iter_get_basic(iter, &valu32);
		break;
	case DBUS_TYPE_UINT16:
		dbus_message_iter_get_basic(iter, &valu16);
        if (valu16 == 0x4c) {
            found = true;
        }
		break;
	case DBUS_TYPE_INT16:
		dbus_message_iter_get_basic(iter, &vals16);
		break;
	case DBUS_TYPE_BYTE:
		dbus_message_iter_get_basic(iter, &byte);
		break;
	case DBUS_TYPE_VARIANT:
		dbus_message_iter_recurse(iter, &subiter);
		print_iter(label, name, &subiter, found);
		break;
	case DBUS_TYPE_ARRAY:
		dbus_message_iter_recurse(iter, &subiter);

		if (dbus_type_is_fixed(
				dbus_message_iter_get_arg_type(&subiter))) {
			print_fixed_iter(label, name, &subiter, found);
			break;
		}

		while (dbus_message_iter_get_arg_type(&subiter) !=
							DBUS_TYPE_INVALID) {
			print_iter(label, name, &subiter, found);
			dbus_message_iter_next(&subiter);
		}
		break;
	case DBUS_TYPE_DICT_ENTRY:
		dbus_message_iter_recurse(iter, &subiter);
		entry = g_strconcat(name, " Key", NULL);
		found = print_iter(label, entry, &subiter, found);
		g_free(entry);

		entry = g_strconcat(name, " Value", NULL);
		dbus_message_iter_next(&subiter);
		print_iter(label, entry, &subiter, found);
		g_free(entry);
		break;
	default:
		break;
	}

    return found;
}

static void print_property(GDBusProxy *proxy, const char *name)
{
	DBusMessageIter iter;

	if (g_dbus_proxy_get_property(proxy, name, &iter) == FALSE)
		return;

	print_iter("\t", name, &iter, false);
}

static void proxy_added(GDBusProxy *proxy, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);

	if (!strcmp(interface, "org.bluez.Device1")) {
        //device_added(proxy);
    } else if (!strcmp(interface, "org.bluez.Adapter1")) {
		adapter_added(proxy);
    }
}

static void proxy_removed(GDBusProxy *proxy, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);

	if (!strcmp(interface, "org.bluez.Device1")) {
        //printf("REMOVED device\n");
    }
}

static void property_changed(GDBusProxy *proxy, const char *name,
					DBusMessageIter *iter, void *user_data)
{
	const char *interface;

	interface = g_dbus_proxy_get_interface(proxy);

	if (!strcmp(interface, "org.bluez.Device1")) {
        DBusMessageIter addr_iter;
        char *str;

        if (g_dbus_proxy_get_property(proxy, "Address",
                                      &addr_iter) == TRUE) {
            const char *address;

            dbus_message_iter_get_basic(&addr_iter,
                                        &address);
            str = g_strdup_printf("Device %s ", address);
        } else {
            str = g_strdup("");
        }

        print_iter(str, name, iter, false);
        g_free(str);
    }
}

int main(int argc, char *argv[])
{
    GMainLoop *main_loop;
	GDBusClient *client;
	int status;

    if (argc == 1) {
        OUTPUT = stdout;
    } else if (argc == 2) {
        OUTPUT = fopen(argv[1], "w+");
        if (OUTPUT == NULL) {
            perror("Can't open file");
            exit(-1);
        }
    } else {
        printf("Usage: %s [output_file]\n", argv[0]);
        exit(-1);
    }

    fprintf(OUTPUT, "L: NA R: NA C: NA\n");
    fflush(OUTPUT);

	dbus_conn = g_dbus_setup_bus(DBUS_BUS_SYSTEM, NULL, NULL);

	client = g_dbus_client_new(dbus_conn, "org.bluez", "/org/bluez");

	g_dbus_client_set_proxy_handlers(client, proxy_added, proxy_removed,
							property_changed, NULL);

    main_loop = g_main_loop_new(NULL, FALSE);
    g_main_loop_run(main_loop);
    g_main_loop_unref(main_loop);

	g_dbus_client_unref(client);

	dbus_connection_unref(dbus_conn);

	return status;
}
