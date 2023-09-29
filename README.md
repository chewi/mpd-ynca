# mpd-ynca

An MPD client to control Yamaha AV receivers with YNCA (network control) support.

## Features

* Powers the receiver on and switches input when playback starts.
* Delays playback for a configurable time while the receiver powers on.
* Switches sound program to a per-song choice, "Straight" for multi-channel songs, or some default.
* Configurable using an INI file.

## Requirements

Meson is used to build and install. The following additional dependencies are required, including their development files.

* libmpdclient
* Boost

## Compilation

Run the following.

```
mkdir build
cd build
meson --sysconfdir=/etc
ninja
```

## Installation

Install the `mpd-ynca` binary anywhere you like.

Put a copy of `ynca.conf` next to your MPD configuration. mpd-ynca respects the `HOME` and `XDG_CONFIG_HOME` environment variables so it will look for the configuration file in the following locations:

* `${XDG_CONFIG_HOME}/mpd/ynca.conf`, e.g. `~/.config/mpd/ynca.conf`
* `${HOME}/.mpd-ynca.conf`
* `/etc/mpd-ynca.conf`

## Configuration

Edit the aforementioned configuration file in your favourite text editor. The settings are documented there.

## Usage

mpd-ynca locates the MPD server by making use of the `MPD_HOST` and `MPD_PORT` environment variables in the same way that some other MPD clients such as `mpc` do. The default host and port is `localhost:6600`. The client can be started before or after starting the MPD server because it will automatically attempt to reconnect once a second.

To set a per-song sound program, use `mpc` or some other appropriate client to set the `ynca_program` sticker. For example:

```
mpc sticker "Machinae Supremacy/Machinae Supremacy - Redeemer/01 - Machinae Supremacy - Elite.m4a" set ynca_program "Hall in Vienna"
```

## Notes

### Input switching behaviour

It is not possible for MPD clients to accurately tell the difference between the playlist naturally advancing to the next song and a user manually jumping to the next song. If a user switches the receiver to a different input whilst MPD is still playing, we don't want mpd-ynca to automatically switch the receiver back again when the next song starts. To prevent this, mpd-ynca only automatically switches if MPD was previously paused or stopped. This would be equally annoying if MPD were to continue silently playing for a long time as mpd-ynca would refuse to switch for the duration. It therefore also stops playback if it detects that the input has changed when the next song is reached. It does not immediately pause playback as this would involve being continuously connected to the receiver. Some models of receiver can only have one YNCA client connected at a time so this would block other clients from connecting.

### Powering off the receiver when using HDMI

This is not really specific to mpd-ynca but if you power off the receiver when using HDMI, you may find that MPD keeps looping over a short few seconds as it repeatedly fails to play the next segment. mpd-ynca will not do anything while this is happening as the song is not changing. It would be nice if MPD could detect this repeated failure and pause itself.

### MPD connection error: no such sticker

If you have set `default_program` then you will see the above error message on stderr every time you play a song without a `ynca_program` sticker. This error comes from libmpdclient rather than mpd-client. It cannot be avoided without checking whether the sticker exists before attempting to retreive its value and this would be needlessly expensive. Send stderr to `/dev/null` if it bothers you.

### Race conditions

It appears that the YNCA protocol is subject to race conditions. As such, mpd-ynca may do the wrong thing, such as set the sound program against the wrong input, if you manually switch the input just as the song changes.

## TODO

* Have mpd-ynca do nothing when the MPD audio output for the receiver is not enabled.

## Author

James ["Chewi"](https://github.com/chewi) Le Cuirot

## License

mpd-ynca is free software; you can redistribute it and/or modify it under the terms of the [GNU General Public License](https://www.gnu.org/licenses/old-licenses/gpl-2.0.en.html) as published by the Free Software Foundation; either version 2 of the License, or (at your option) any later version.
