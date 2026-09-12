// Copy this file to src/pkjs/config.js and fill in your endpoint.
// src/pkjs/config.js is gitignored so your URL/tokens never get committed.
//
// The watchface shows the string found at `textPath` in the JSON response.

module.exports = {
  url: 'https://example.com/api/message',
  textPath: 'message',
  headers: {
    // 'Authorization': 'Bearer YOUR_TOKEN',
  }
};
