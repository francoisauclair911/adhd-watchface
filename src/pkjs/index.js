'use strict';

// Hosted settings page URL (GitHub Pages).
// Replace with your own URL after pushing settings/index.html to GitHub Pages.
var SETTINGS_URL = 'https://YOUR_GITHUB_USERNAME.github.io/YOUR_REPO/settings/';

var config = null;
try { config = require('config'); } catch (e) { config = null; }

var FETCH_TIMEOUT_MS = 8000;
var MAX_TEXT_LEN = 120;

var currentSettings = {
  url:      (config && config.url)      || '',
  textPath: (config && config.textPath) || 'message',
  pollSec:  (config && config.pollMs)   ? Math.round(config.pollMs / 1000) : 180,
  headers:  []
};

function loadSettings() {
  try {
    var stored = localStorage.getItem('adhd_settings');
    if (stored) {
      var parsed = JSON.parse(stored);
      if (parsed.url      !== undefined) currentSettings.url      = parsed.url;
      if (parsed.textPath !== undefined) currentSettings.textPath = parsed.textPath;
      if (parsed.pollSec  !== undefined) currentSettings.pollSec  = parsed.pollSec;
      if (Array.isArray(parsed.headers)) currentSettings.headers  = parsed.headers;
    }
  } catch (e) {
    console.log('Failed to load settings: ' + e);
  }
}

function saveSettings(s) {
  try {
    localStorage.setItem('adhd_settings', JSON.stringify(s));
  } catch (e) {
    console.log('Failed to save settings: ' + e);
  }
}

function getHeaders() {
  var out = {};
  if (config && config.headers) {
    for (var k in config.headers) {
      if (config.headers.hasOwnProperty(k)) out[k] = config.headers[k];
    }
  }
  (currentSettings.headers || []).forEach(function(h) {
    if (h.key) out[h.key] = h.value;
  });
  return out;
}

function getText(data, textPath) {
  var cur = data;
  var parts = textPath.split('.');
  for (var i = 0; i < parts.length; i++) {
    if (cur == null) { return null; }
    cur = cur[parts[i]];
  }
  return cur;
}

var lastSentText = null;
var inFlight = false;
var pollTimer = null;

function sendText(text) {
  var msg = String(text).substring(0, MAX_TEXT_LEN);
  if (msg === lastSentText) {
    console.log('No change, skipping send.');
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
  var url = currentSettings.url;
  if (!url) {
    console.log('No endpoint configured — open settings to set one.');
    return;
  }

  if (inFlight) {
    console.log('Request in flight, skipping poll.');
    return;
  }

  console.log('Fetching ' + url);
  inFlight = true;

  var textPath = currentSettings.textPath || 'message';
  var headers  = getHeaders();
  var settled  = false;

  var timer = setTimeout(function() {
    if (settled) { return; }
    settled = true;
    inFlight = false;
    req.abort();
    sendText('offline');
  }, FETCH_TIMEOUT_MS);

  var req = new XMLHttpRequest();
  req.open('GET', url, true);
  for (var h in headers) {
    if (headers.hasOwnProperty(h)) req.setRequestHeader(h, headers[h]);
  }
  req.onload = function() {
    if (settled) { return; }
    settled = true;
    inFlight = false;
    clearTimeout(timer);
    if (req.status !== 200) {
      sendText('offline');
      return;
    }
    var text = null;
    try { text = getText(JSON.parse(req.responseText), textPath); } catch (e) {}
    if (typeof text !== 'string' || text.length === 0) {
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

function startPolling() {
  if (pollTimer) { clearInterval(pollTimer); }
  var ms = (currentSettings.pollSec || 180) * 1000;
  pollTimer = setInterval(fetchText, ms);
}

Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS ready');
  loadSettings();
  fetchText();
  startPolling();
});

Pebble.addEventListener('showConfiguration', function() {
  var params = encodeURIComponent(JSON.stringify({
    url:      currentSettings.url,
    textPath: currentSettings.textPath,
    pollSec:  currentSettings.pollSec,
    headers:  currentSettings.headers
  }));
  Pebble.openURL(SETTINGS_URL + '?settings=' + params);
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e.response || e.response === 'CANCELLED') { return; }
  try {
    var s = JSON.parse(decodeURIComponent(e.response));
    currentSettings.url      = s.url      || currentSettings.url;
    currentSettings.textPath = s.textPath || currentSettings.textPath;
    currentSettings.pollSec  = s.pollSec  || currentSettings.pollSec;
    currentSettings.headers  = Array.isArray(s.headers) ? s.headers : currentSettings.headers;
    saveSettings(currentSettings);
    lastSentText = null;
    startPolling();
    fetchText();
    console.log('Settings updated: ' + JSON.stringify(currentSettings));
  } catch (err) {
    console.log('Failed to parse settings: ' + err);
  }
});

Pebble.addEventListener('appmessage', function(e) {
  if (e.payload && e.payload.fetch === 1) { fetchText(); }
});
