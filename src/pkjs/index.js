'use strict';

var config = null;
try {
  config = require('config');
} catch (e) {
  config = null;
}

var ENDPOINT_URL = config && config.url;
var TEXT_PATH = (config && config.textPath) || 'message';
var HEADERS = (config && config.headers) || {};
var POLL_MS = (config && config.pollMs) || (3 * 60 * 1000);
var FETCH_TIMEOUT_MS = 8000;
var MAX_TEXT_LEN = 120;

var lastSentText = null;
var inFlight = false;

function getText(data) {
  var cur = data;
  var parts = TEXT_PATH.split('.');
  for (var i = 0; i < parts.length; i++) {
    if (cur == null) { return null; }
    cur = cur[parts[i]];
  }
  return cur;
}

function sendText(text) {
  var msg = String(text).substring(0, MAX_TEXT_LEN);
  if (msg === lastSentText) {
    console.log('No change, skipping send: ' + msg);
    return;
  }
  lastSentText = msg;
  Pebble.sendAppMessage({ apiText: msg }, function() {
    console.log('Sent to watch: ' + msg);
  }, function(e) {
    console.log('Send failed: ' + e.error);
    lastSentText = null;
  });
}

function fetchText() {
  if (!ENDPOINT_URL) {
    console.log('No endpoint configured. Add src/pkjs/config.js (see config.example.js).');
    return;
  }

  if (inFlight) {
    console.log('Request already in flight, skipping poll.');
    return;
  }

  console.log('Fetching ' + ENDPOINT_URL);
  inFlight = true;

  var settled = false;
  var timer = setTimeout(function() {
    if (settled) { return; }
    settled = true;
    inFlight = false;
    req.abort();
    sendText('offline');
  }, FETCH_TIMEOUT_MS);

  var req = new XMLHttpRequest();
  req.open('GET', ENDPOINT_URL, true);
  for (var header in HEADERS) {
    if (HEADERS.hasOwnProperty(header)) {
      req.setRequestHeader(header, HEADERS[header]);
    }
  }
  req.onload = function() {
    if (settled) { return; }
    settled = true;
    inFlight = false;
    clearTimeout(timer);
    if (req.status !== 200) {
      console.log('HTTP ' + req.status);
      sendText('offline');
      return;
    }
    var text = null;
    try {
      text = getText(JSON.parse(req.responseText));
    } catch (err) {
      text = null;
      console.log('Response was not valid JSON');
    }
    if (typeof text !== 'string' || text.length === 0) {
      console.log('No string at textPath "' + TEXT_PATH + '"');
      sendText('no data');
      return;
    }
    sendText(text);
  };
  req.onerror = function() {
    if (settled) { return; }
    settled = true;
    inFlight = false;
    clearTimeout(timer);
    sendText('offline');
  };
  req.send(null);
}

Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS ready');
  fetchText();
  setInterval(fetchText, POLL_MS);
});

Pebble.addEventListener('appmessage', function(e) {
  if (e.payload && e.payload.fetch === 1) {
    fetchText();
  }
});
