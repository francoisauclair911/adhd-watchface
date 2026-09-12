'use strict';

// Hosted settings page (GitHub Pages)
var SETTINGS_URL = 'https://francoisauclair911.github.io/adhd-watchface/';

var config = null;
try { config = require('config'); } catch (e) { config = null; }

var FETCH_TIMEOUT_MS = 8000;
var MAX_TEXT_LEN = 120;
var TODOIST_FILTER_URL  = 'https://api.todoist.com/api/v1/tasks/filter';
var TODOIST_CLOSE_URL   = 'https://api.todoist.com/api/v1/tasks/{id}/close';

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
var currentTaskId = null;

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
    sendText('configure todoist');
    return;
  }

  var url = TODOIST_FILTER_URL + '?query=' + encodeURIComponent('@' + label) + '&limit=1';
  console.log('Fetching Todoist: ' + url);

  var settled = false;
  var timer = setTimeout(function() {
    if (settled) { return; }
    settled = true; inFlight = false;
    req.abort();
    sendText('offline');
  }, FETCH_TIMEOUT_MS);

  var req = new XMLHttpRequest();
  req.open('GET', url, true);
  req.setRequestHeader('Authorization', 'Bearer ' + token);
  req.onload = function() {
    if (settled) { return; }
    settled = true; inFlight = false;
    clearTimeout(timer);
    if (req.status === 401 || req.status === 403) { sendText('auth error'); return; }
    if (req.status !== 200) { sendText('offline'); return; }
    try {
      var data    = JSON.parse(req.responseText);
      var results = data.results;
      if (!results || results.length === 0) {
        currentTaskId = null;
        sendText('all done!');
        saveCachedText('all done!');
        return;
      }
      currentTaskId = results[0].id;
      var title = results[0].content;
      if (typeof title !== 'string' || title.length === 0) {
        sendText('all done!');
        saveCachedText('all done!');
        return;
      }
      sendText(title);
      saveCachedText(title);
    } catch (e) { sendText('offline'); }
  };
  req.onerror = function() {
    if (settled) { return; }
    settled = true; inFlight = false;
    clearTimeout(timer);
    sendText('offline');
  };
  req.send(null);
}

function fetchEndpoint() {
  var url = currentSettings.url;
  if (!url) { return; }

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
  var timer = setTimeout(function() {
    if (settled) { return; }
    settled = true; inFlight = false;
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
    settled = true; inFlight = false;
    clearTimeout(timer);
    if (req.status !== 200) { sendText('offline'); return; }
    var text = null;
    try { text = getText(JSON.parse(req.responseText), textPath); } catch (e) {}
    if (typeof text !== 'string' || text.length === 0) { sendText('no data'); return; }
    sendText(text);
    saveCachedText(text);
  };
  req.onerror = function() {
    if (settled) { return; }
    settled = true; inFlight = false;
    clearTimeout(timer);
    sendText('offline');
  };
  req.send(null);
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

function completeCurrentTask() {
  if (currentSettings.source !== 'todoist') { return; }
  var token  = currentSettings.todoistToken;
  var taskId = currentTaskId;
  if (!token) { return; }
  if (!taskId) {
    console.log('No task ID yet — fetching instead.');
    lastSentText = null;
    fetchText();
    return;
  }

  console.log('Completing task: ' + taskId);
  var url = TODOIST_CLOSE_URL.replace('{id}', taskId);
  var req = new XMLHttpRequest();
  req.open('POST', url, true);
  req.setRequestHeader('Authorization', 'Bearer ' + token);
  req.setRequestHeader('Content-Length', '0');
  req.onload = function() {
    if (req.status === 204 || req.status === 200) {
      console.log('Task completed: ' + taskId);
      currentTaskId = null;
      lastSentText  = null;
      fetchText();
    } else {
      console.log('Complete failed: HTTP ' + req.status);
    }
  };
  req.onerror = function() { console.log('Complete request failed'); };
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
    lastSentText  = null;
    currentTaskId = null;
    startPolling();
    fetchText();
    console.log('Settings updated: ' + JSON.stringify(currentSettings));
  } catch (err) {
    console.log('Failed to parse settings: ' + err);
  }
});

Pebble.addEventListener('appmessage', function(e) {
  if (!e.payload) { return; }
  if (e.payload.fetch        === 1) { fetchText(); }
  if (e.payload.completeTask === 1) { completeCurrentTask(); }
});
