# ADHD Actions

Companion Emery watchapp for the ADHD watchface on Pebble Time 2.

The watchface remains the always-visible display. This app provides a hardware-button menu with:

- `Complete`: complete the current Todoist task and fetch the next one.
- `Refresh`: fetch the current task immediately.

Configuration uses the same hosted settings page and fields as the watchface:

`https://francoisauclair911.github.io/adhd-watchface/`

The companion has its own settings storage, so configure the Todoist token and label once for this app as well.

Build and install:

```sh
pebble build
pebble install --emulator emery
```
