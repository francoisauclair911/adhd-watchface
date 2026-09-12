# adhd-watchface

An ADHD-friendly watchface for the Core Pebble Time 2 (emery, 200x228, 64-color).

- Huge 12-hour time (Bebas Neue, full-bleed)
- Date (weekday month day)
- Battery icon + percentage (yellow when charging, red when low)
- One-line text from your own API, refreshed every 30 min (top line, accent color)

## Building & running

```sh
pebble build                          # build for emery
pebble install --emulator emery       # install on the emery emulator
pebble install --cloudpebble          # install over the phone app (Dev Connect)
```

## API text

The watchface pulls one line of text from your own endpoint via PebbleKit JS:

1. `cp config.example.js src/pkjs/config.js` (gitignored — never committed)
2. Set `url`, `textPath` (JSON path to the string, e.g. `message` or `data.text`),
   and optional `headers` (e.g. auth token).
3. Rebuild. Without `config.js` the face shows a reminder to add it.

## Target platforms

`targetPlatforms` in `package.json` controls which watches you build for. This
project targets **emery** only (Core Pebble Time 2, 200x228).

## Project layout

```
src/c/            C source for the watchface
src/pkjs/         PebbleKit JS (phone-side) source
resources/fonts/  Bebas Neue (OFL) used for the big time
config.example.js Template for the gitignored src/pkjs/config.js
package.json      Project metadata (UUID, platforms, resources, message keys)
wscript           Build rules — usually no need to edit
```

## Documentation

Full SDK docs, tutorials, and API reference: <https://developer.repebble.com>
