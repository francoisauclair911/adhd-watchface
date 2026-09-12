// Compile-time fallback defaults — used only if no settings have been saved
// via the phone app settings page. Phone settings always take precedence.
//
// Copy this file to src/pkjs/config.js and fill in your values.
// src/pkjs/config.js is gitignored so your URL/tokens never get committed.

module.exports = {
  url: 'https://example.com/api/message',
  textPath: 'message',
  pollMs: 180000,  // poll interval in ms (default: 3 minutes)
  headers: {
    // 'Authorization': 'Bearer YOUR_TOKEN',
  }
};
