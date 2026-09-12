'use strict';

var SETTINGS_URL = 'https://francoisauclair911.github.io/adhd-watchface/';
var FETCH_TIMEOUT_MS = 8000;
var MAX_TEXT_LEN = 120;
var TODOIST_FILTER_URL = 'https://api.todoist.com/api/v1/tasks/filter';
var TODOIST_CLOSE_URL = 'https://api.todoist.com/api/v1/tasks/{id}/close';
var ACTION_REFRESH = 1;
var ACTION_COMPLETE = 2;

var config = {
  url: 'https://jsonplaceholder.typicode.com/todos/5',
  textPath: 'title',
  headers: {}
};

var currentSettings = {
  source: 'endpoint',
  url: config.url,
  textPath: config.textPath,
  headers: [],
  todoistToken: '',
  todoistLabel: '',
  pollSec: 180
};

var currentTaskId = null;
var requestInFlight = false;
var lastSentText = null;

function loadSettings() {
  try {
    var stored = localStorage.getItem('adhd_settings');
    if (!stored) { return; }
    var parsed = JSON.parse(stored);
    ['source', 'url', 'textPath', 'todoistToken', 'todoistLabel', 'pollSec'].forEach(function(k) {
      if (parsed[k] !== undefined) { currentSettings[k] = parsed[k]; }
    });
    if (Array.isArray(parsed.headers)) { currentSettings.headers = parsed.headers; }
  } catch (e) {
    console.log('Failed to load settings: ' + e);
  }
}

function saveSettings(settings) {
  try { localStorage.setItem('adhd_settings', JSON.stringify(settings)); }
  catch (e) { console.log('Failed to save settings: ' + e); }
}

function sendText(text) {
  var msg = String(text).substring(0, MAX_TEXT_LEN);
  lastSentText = msg;
  Pebble.sendAppMessage({ apiText: msg }, function() {
    console.log('Sent: ' + msg);
  }, function(e) {
    console.log('Send failed: ' + e.error);
    lastSentText = null;
  });
}

function getText(data, textPath) {
  var current = data;
  textPath.split('.').forEach(function(part) {
    if (current != null) { current = current[part]; }
  });
  return current;
}

function request(url, headers, callback) {
  var settled = false;
  var req = null;
  var timer = setTimeout(function() {
    try { if (req) { req.abort(); } } catch (e) {}
    finish('timeout');
  }, FETCH_TIMEOUT_MS);

  function finish(error) {
    if (settled) { return; }
    settled = true;
    clearTimeout(timer);
    requestInFlight = false;
    callback(error, req);
  }

  try {
    req = new XMLHttpRequest();
    req.open('GET', url, true);
    for (var key in headers) {
      if (headers.hasOwnProperty(key)) { req.setRequestHeader(key, headers[key]); }
    }
    req.onload = function() {
      finish(req.status === 200 ? null : 'http ' + req.status);
    };
    req.onerror = function() { finish('network'); };
    req.send(null);
  } catch (e) {
    finish('setup: ' + e);
  }
}

function fetchTask() {
  if (requestInFlight) { return; }
  requestInFlight = true;

  if (currentSettings.source === 'todoist') {
    var token = currentSettings.todoistToken;
    var label = currentSettings.todoistLabel;
    if (!token || !label) {
      requestInFlight = false;
      sendText('set up todoist');
      return;
    }

    var todoistUrl = TODOIST_FILTER_URL + '?query=' +
      encodeURIComponent('@' + label) + '&limit=1';
    request(todoistUrl, { Authorization: 'Bearer ' + token }, function(error, req) {
      if (error) {
        sendText(error === 'http 401' || error === 'http 403' ? 'auth error' : 'offline');
        return;
      }
      try {
        var results = JSON.parse(req.responseText).results;
        if (Array.isArray(results) && results.length && results[0].content) {
          currentTaskId = results[0].id;
          sendText(results[0].content);
        } else {
          currentTaskId = null;
          sendText('all done!');
        }
      } catch (e) {
        sendText('offline');
      }
    });
    return;
  }

  var headers = {};
  (currentSettings.headers || []).forEach(function(header) {
    if (header.key) { headers[header.key] = header.value; }
  });
  if (!currentSettings.url) {
    requestInFlight = false;
    sendText('set up endpoint');
    return;
  }
  request(currentSettings.url, headers, function(error, req) {
    if (error) { sendText('offline'); return; }
    try {
      var text = getText(JSON.parse(req.responseText), currentSettings.textPath || 'message');
      sendText(typeof text === 'string' && text.length ? text : 'no data');
    } catch (e) {
      sendText('no data');
    }
  });
}

function completeTask() {
  if (currentSettings.source !== 'todoist') {
    sendText('Todoist only');
    return;
  }
  if (!currentTaskId) {
    sendText('Refresh first');
    return;
  }

  var req = new XMLHttpRequest();
  var url = TODOIST_CLOSE_URL.replace('{id}', currentTaskId);
  try {
    req.open('POST', url, true);
    req.setRequestHeader('Authorization', 'Bearer ' + currentSettings.todoistToken);
    req.setRequestHeader('Content-Length', '0');
    req.onload = function() {
      if (req.status === 200 || req.status === 204) {
        currentTaskId = null;
        lastSentText = null;
        fetchTask();
      } else {
        sendText(req.status === 401 || req.status === 403 ? 'auth error' : 'offline');
      }
    };
    req.onerror = function() { sendText('offline'); };
    req.send(null);
  } catch (e) {
    sendText('offline');
  }
}

Pebble.addEventListener('ready', function() {
  loadSettings();
  fetchTask();
});

Pebble.addEventListener('showConfiguration', function() {
  var params = encodeURIComponent(JSON.stringify({
    source: currentSettings.source,
    url: currentSettings.url,
    textPath: currentSettings.textPath,
    headers: currentSettings.headers,
    todoistToken: currentSettings.todoistToken,
    todoistLabel: currentSettings.todoistLabel,
    pollSec: currentSettings.pollSec
  }));
  Pebble.openURL(SETTINGS_URL + '?settings=' + params);
});

Pebble.addEventListener('webviewclosed', function(e) {
  if (!e.response || e.response === 'CANCELLED') { return; }
  try {
    var raw = e.response;
    if (raw.charAt(0) === '#') { raw = raw.slice(1); }
    var settings = JSON.parse(decodeURIComponent(raw));
    ['source', 'url', 'textPath', 'todoistToken', 'todoistLabel', 'pollSec'].forEach(function(k) {
      if (settings[k] !== undefined) { currentSettings[k] = settings[k]; }
    });
    if (Array.isArray(settings.headers)) { currentSettings.headers = settings.headers; }
    saveSettings(currentSettings);
    lastSentText = null;
    fetchTask();
  } catch (err) {
    console.log('Failed to parse settings: ' + err);
  }
});

Pebble.addEventListener('appmessage', function(e) {
  if (!e.payload || e.payload.action === undefined) { return; }
  lastSentText = null;
  if (e.payload.action === ACTION_COMPLETE) { completeTask(); }
  if (e.payload.action === ACTION_REFRESH) { fetchTask(); }
});
