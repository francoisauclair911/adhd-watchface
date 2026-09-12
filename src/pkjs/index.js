'use strict';

// Hosted settings page (GitHub Pages)
var SETTINGS_URL = 'https://francoisauclair911.github.io/adhd-watchface/';

// Bundled defaults for the 'endpoint' source. The settings page (localStorage)
// overrides these. NOTE: require('config') is broken in SDK webpack builds
// (_message_key_wrapper rejects every module except message_keys), so the
// defaults are inlined here instead.
var config = {
  url:      'https://jsonplaceholder.typicode.com/todos/5',
  textPath: 'title',
  headers:  {}
};

var FETCH_TIMEOUT_MS = 8000;
var MAX_TEXT_LEN = 120;
var TODOIST_FILTER_URL  = 'https://api.todoist.com/api/v1/tasks/filter';

var currentSettings = {
  source:       'endpoint',
  url:          (config && config.url)      || '',
  textPath:     (config && config.textPath) || 'message',
  headers:      [],
  todoistToken: '',
  todoistLabel: '',
  pollSec:      (config && config.pollMs) ? Math.round(config.pollMs / 1000) : 180
};

function loadSettings() {
  try {
    var stored = localStorage.getItem('adhd_settings');
    if (stored) {
      var parsed = JSON.parse(stored);
      ['source','url','textPath','todoistToken','todoistLabel','pollSec'].forEach(function(k) {
        if (parsed[k] !== undefined) currentSettings[k] = parsed[k];
      });
      if (Array.isArray(parsed.headers)) currentSettings.headers = parsed.headers;
    }
  } catch (e) { console.log('Failed to load settings: ' + e); }
}

function saveSettings(s) {
  try { localStorage.setItem('adhd_settings', JSON.stringify(s)); }
  catch (e) { console.log('Failed to save settings: ' + e); }
}

function loadCachedText() {
  try { return localStorage.getItem('adhd_last_text') || null; } catch (e) { return null; }
}

function saveCachedText(text) {
  try { localStorage.setItem('adhd_last_text', text); } catch (e) {}
}

var lastSentText  = null;
var inFlight      = false;
var pollTimer     = null;

function sendText(text) {
  var msg = String(text).substring(0, MAX_TEXT_LEN);
  if (msg === lastSentText) { return; }
  lastSentText = msg;
  Pebble.sendAppMessage({ apiText: msg }, function() {
    console.log('Sent: ' + msg);
  }, function(e) {
    console.log('Send failed: ' + e.error);
    lastSentText = null;
  });
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

function fetchTodoist() {
  var token = currentSettings.todoistToken;
  var label = currentSettings.todoistLabel;

  if (!token || !label) {
    inFlight = false;
    sendText('set up todoist');
    return;
  }

  var url = TODOIST_FILTER_URL + '?query=' + encodeURIComponent('@' + label) + '&limit=1';
  console.log('Fetching Todoist: ' + url);

  var settled = false;
  var req     = null;
  var timer   = null;

  function done(err) {
    if (settled) { return; }
    settled = true;
    inFlight = false;
    if (timer) { clearTimeout(timer); }
    if (err) {
      console.log('Todoist fetch failed: ' + err);
      sendText(err === 'auth' ? 'auth error' : 'offline');
      return;
    }
    var text = null;
    try {
      var data    = JSON.parse(req.responseText);
      var results = data && data.results;
      if (Array.isArray(results) && results.length > 0 &&
          typeof results[0].content === 'string' && results[0].content.length > 0) {
        text = results[0].content;
      }
    } catch (e) {}
    if (text === null) {
      text = 'all done!';
    }
    sendText(text);
    saveCachedText(text);
  }

  timer = setTimeout(function() {
    try { if (req) { req.abort(); } } catch (e) {}
    done('timeout');
  }, FETCH_TIMEOUT_MS);

  try {
    req = new XMLHttpRequest();
    req.open('GET', url, true);
    req.setRequestHeader('Authorization', 'Bearer ' + token);
    req.onload = function() {
      if (settled) { return; }
      if (req.status === 401 || req.status === 403) { done('auth'); return; }
      if (req.status !== 200) { done('http ' + req.status); return; }
      done(null);
    };
    req.onerror = function() { done('network'); };
    req.send(null);
  } catch (e) {
    done('setup: ' + e);
  }
}

function fetchEndpoint() {
  var url = currentSettings.url;
  if (!url) {
    inFlight = false;
    sendText('set up endpoint');
    return;
  }

  var headers = {};
  if (config && config.headers) {
    for (var k in config.headers) {
      if (config.headers.hasOwnProperty(k)) headers[k] = config.headers[k];
    }
  }
  (currentSettings.headers || []).forEach(function(h) {
    if (h.key) headers[h.key] = h.value;
  });

  var textPath = currentSettings.textPath || 'message';
  console.log('Fetching endpoint: ' + url);

  var settled = false;
  var req     = null;
  var timer   = null;

  function done(err) {
    if (settled) { return; }
    settled = true;
    inFlight = false;
    if (timer) { clearTimeout(timer); }
    if (err) {
      console.log('Endpoint fetch failed: ' + err);
      sendText('offline');
      return;
    }
    var text = null;
    try { text = getText(JSON.parse(req.responseText), textPath); } catch (e) {}
    if (typeof text !== 'string' || text.length === 0) { sendText('no data'); return; }
    sendText(text);
    saveCachedText(text);
  }

  timer = setTimeout(function() {
    try { if (req) { req.abort(); } } catch (e) {}
    done('timeout');
  }, FETCH_TIMEOUT_MS);

  try {
    req = new XMLHttpRequest();
    req.open('GET', url, true);
    for (var h in headers) {
      if (headers.hasOwnProperty(h)) req.setRequestHeader(h, headers[h]);
    }
    req.onload = function() {
      if (settled) { return; }
      done(req.status !== 200 ? ('http ' + req.status) : null);
    };
    req.onerror = function() { done('network'); };
    req.send(null);
  } catch (e) {
    done('setup: ' + e);
  }
}

function fetchText() {
  if (inFlight) { return; }
  inFlight = true;
  if (currentSettings.source === 'todoist') {
    fetchTodoist();
  } else {
    fetchEndpoint();
  }
}

function startPolling() {
  if (pollTimer) { clearInterval(pollTimer); }
  var ms = (currentSettings.pollSec || 180) * 1000;
  pollTimer = setInterval(fetchText, ms);
}

Pebble.addEventListener('ready', function() {
  console.log('PebbleKit JS ready');
  loadSettings();

  // Show cached text immediately, then fetch fresh in background
  var cached = loadCachedText();
  if (cached) {
    lastSentText = null;
    sendText(cached);
  }

  fetchText();
  startPolling();
});

Pebble.addEventListener('showConfiguration', function() {
  var params = encodeURIComponent(JSON.stringify({
    source:       currentSettings.source,
    url:          currentSettings.url,
    textPath:     currentSettings.textPath,
    headers:      currentSettings.headers,
    todoistToken: currentSettings.todoistToken,
    todoistLabel: currentSettings.todoistLabel,
    pollSec:      currentSettings.pollSec
  }));
  Pebble.openURL(SETTINGS_URL + '?settings=' + params);
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e.response || e.response === 'CANCELLED') { return; }
  try {
    var raw = e.response;
    if (raw.charAt(0) === '#') { raw = raw.slice(1); }
    var s = JSON.parse(decodeURIComponent(raw));
    ['source','url','textPath','todoistToken','todoistLabel','pollSec'].forEach(function(k) {
      if (s[k] !== undefined) currentSettings[k] = s[k];
    });
    if (Array.isArray(s.headers)) currentSettings.headers = s.headers;
    saveSettings(currentSettings);
    lastSentText = null;
    startPolling();
    fetchText();
    console.log('Settings updated: ' + JSON.stringify(currentSettings));
  } catch (err) {
    console.log('Failed to parse settings: ' + err);
  }
});
