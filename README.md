# apodsmon

A CLI utility to monitor the AirPods battery in Linux, using
BlueZ. This is based on BlueZ's `bluetooh-player.c` and
[OpenPods](https://github.com/adolfintel/OpenPods).

This was written in a rush to scratch a personal itch, so don't expect
anything fancy from this.

## Build

*apodsmon* depends on `dbus-1`, `glib-2.0` and `gio-2.0`. I didn't
bother to implement `autotools` nor any other build system, so you may
need to tune `Makefile` by hand.

Once you're ready, simply run `make`.

## Using

```
Usage: apodsmon [output_file]
```

I'm using *apodsmon* to feed my [i3status](https://i3wm.org/i3status/)
bar so, once executed, it keeps running monitoring for AirPods
iBeacons, and printing the battery levels to either `stdout`, or
`output_file` if specified in the command line.

I'm starting it from a `systemd` service like this:

```
[Unit]
Description=AirPods Battery Monitor

[Service]
ExecStart=~/bin/apodsmon /tmp/apodsmon.out

[Install]
WantedBy=default.target
```

Then I'm using this script as a wrapper for `i3status`:

```
#!/bin/sh
# shell script to prepend i3status with more stuff

i3status | while :
do
    read line
    apodmon=`tail -1 /tmp/apodsmon.out`
    if [ -z "$apodmon" ]; then
        echo "$line" || exit 1
    else
        echo "$apodmon | $line" || exit 1
    fi
done
```
