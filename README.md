# adhd-watchface

An ADHD-friendly watchface for the Core Pebble Time 2 (emery, 200x228, 64-color),
styled after the classic "Essential" three-block layout (cf. Aura Essential).

- **Top block** (coral): one line of text from your own API, drawn white with a
  black outline over the color block, refreshed every 30 min
- **White band**: a large segmented (LECO) 12-hour clock in the coral theme color
- **Bottom strip**: coral color strip, closed by black separator lines

## Building & running

```sh
pebble build                          # build for emery
pebble install --emulator emery       # install on the emery emulator
pebble install --cloudpebble          # install over the phone app (Dev Connect)
```

## Companion actions app

`adhd-actions/` is a separate Emery watchapp for Pebble Time 2. It provides
hardware-button actions for `Complete` and `Refresh` while this project remains
the always-visible watchface. Build it from that directory with the same
commands. It reuses the hosted settings page and fields, but settings are stored
separately for each app, so configure the Todoist token and label for both apps.

## API text

The watchface pulls one line of text from your own endpoint via PebbleKit JS:

1. `cp config.example.js src/pkjs/config.js` (gitignored — never committed)
2. Set `url`, `textPath` (JSON path to the string, e.g. `message` or `data.text`),
   and optional `headers` (e.g. auth token).
3. Rebuild. Without `config.js` the face shows a reminder to add it.

## Target platforms

`targetPlatforms` in `package.json` controls which watches you build for. This
project targets **emery** only (Core Pebble Time 2, 200x228).

## Design

The layout mirrors the Aura Essential face by Miguel Angel Baeyens
(itself a homage to Essential by Kiezel): a color block for the complications,
a large segmented clock in a white band, and a color strip below. The three
complication icons are replaced here by a single API-driven text line.

## Project layout

```
src/c/            C source for the watchface
src/pkjs/         PebbleKit JS (phone-side) source
config.example.js Template for the gitignored src/pkjs/config.js
package.json      Project metadata (UUID, platforms, resources, message keys)
wscript           Build rules — usually no need to edit
```

## Documentation

Full SDK docs, tutorials, and API reference: <https://developer.repebble.com>
