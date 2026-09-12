// Copy this file to src/pkjs/config.js and fill in your endpoint.
// src/pkjs/config.js is gitignored so your URL/tokens never get committed.
//
// The watchface shows the string found at `textPath` in the JSON response.

module.exports = {
  url: 'https://example.com/api/message',
  textPath: 'message',
  pollMs: 180000,  // poll interval in ms (default: 3 minutes)
  headers: {
    // 'Authorization': 'Bearer YOUR_TOKEN',
  }
};
