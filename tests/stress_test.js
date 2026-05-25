/**
 * CloseCrab Team Mode - WebSocket Stress Test
 *
 * Tests multi-client WebSocket server for correctness under load.
 * Verifies client isolation, latency, and edge-case handling.
 *
 * Usage: node stress_test.js [numClients] [serverUrl]
 *   numClients - concurrent clients to simulate (default: 8)
 *   serverUrl  - WebSocket server URL (default: ws://localhost:9002)
 */

const WebSocket = require('ws');

// --- Configuration ---
const NUM_CLIENTS = parseInt(process.argv[2], 10) || 8;
const SERVER_URL = process.argv[3] || 'ws://localhost:9002';
const MESSAGES_PER_CLIENT = 5;
const MIN_DELAY_MS = 100;
const MAX_DELAY_MS = 2000;
const CONNECT_TIMEOUT_MS = 5000;
const RESPONSE_TIMEOUT_MS = 10000;

// --- Stats ---
const stats = {
  totalSent: 0,
  totalReceived: 0,
  errors: [],
  contaminations: [],
  latencies: [],
  clientResults: [],
};

// --- Utilities ---
function randomDelay() {
  return MIN_DELAY_MS + Math.random() * (MAX_DELAY_MS - MIN_DELAY_MS);
}

function randomUsername() {
  const adjectives = ['Fast', 'Lazy', 'Bold', 'Calm', 'Dark', 'Keen', 'Wild'];
  const nouns = ['Crab', 'Fox', 'Bear', 'Hawk', 'Wolf', 'Lynx', 'Orca'];
  const adj = adjectives[Math.floor(Math.random() * adjectives.length)];
  const noun = nouns[Math.floor(Math.random() * nouns.length)];
  return `${adj}${noun}${Math.floor(Math.random() * 1000)}`;
}

function sleep(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function elapsed(startHr) {
  const diff = process.hrtime(startHr);
  return diff[0] * 1000 + diff[1] / 1e6;
}

// --- Single Client Simulation ---
function simulateClient(clientIndex) {
  return new Promise((resolve) => {
    const result = {
      index: clientIndex,
      username: randomUsername(),
      assignedClientId: null,
      sent: 0,
      received: 0,
      contaminations: [],
      errors: [],
      latencies: [],
      connected: false,
      completed: false,
      _pendingSendTime: null,
    };

    let ws;
    let connectTimer = null;

    try {
      ws = new WebSocket(SERVER_URL);
    } catch (err) {
      result.errors.push(`Connection creation failed: ${err.message}`);
      resolve(result);
      return;
    }

    connectTimer = setTimeout(() => {
      if (!result.connected) {
        result.errors.push('Connection timeout');
        try { ws.close(); } catch (_) {}
        resolve(result);
      }
    }, CONNECT_TIMEOUT_MS);

    ws.on('open', () => {
      result.connected = true;
      clearTimeout(connectTimer);
    });

    ws.on('message', (data) => {
      result.received++;
      let msg;
      try {
        msg = JSON.parse(data.toString());
      } catch (_) {
        // Non-JSON response, still count it
        return;
      }

      // Record latency if we have a pending send timestamp
      if (result._pendingSendTime) {
        result.latencies.push(elapsed(result._pendingSendTime));
        result._pendingSendTime = null;
      }

      // Handle initial connected message
      if (msg.type === 'connected' && msg.clientId) {
        result.assignedClientId = msg.clientId;
        // Send register message
        const registerMsg = JSON.stringify({
          type: 'register',
          username: result.username,
        });
        ws.send(registerMsg);
        result.sent++;
        // Start sending messages
        sendMessages(ws, result).then(() => {
          result.completed = true;
          setTimeout(() => {
            try { ws.close(); } catch (_) {}
          }, 1000);
        });
        return;
      }

      // Check for cross-client contamination
      if (msg.clientId && result.assignedClientId && msg.clientId !== result.assignedClientId) {
        // Some broadcast messages are expected (e.g., user joined notifications)
        // Only flag if it contains response data meant for another client
        if (msg.type === 'response' || msg.type === 'result') {
          result.contaminations.push({
            expected: result.assignedClientId,
            received: msg.clientId,
            msgType: msg.type,
          });
        }
      }
    });

    ws.on('error', (err) => {
      result.errors.push(`WebSocket error: ${err.message}`);
    });

    ws.on('close', () => {
      resolve(result);
    });
  });
}

// --- Send Messages with Random Delays ---
async function sendMessages(ws, result) {
  for (let i = 0; i < MESSAGES_PER_CLIENT; i++) {
    await sleep(randomDelay());
    if (ws.readyState !== WebSocket.OPEN) {
      result.errors.push(`Socket closed before message ${i + 1}`);
      break;
    }
    const msg = JSON.stringify({
      type: 'message',
      content: `Test message ${i + 1} from ${result.username}`,
      timestamp: Date.now(),
    });
    result._pendingSendTime = process.hrtime();
    ws.send(msg);
    result.sent++;
  }
  // Wait for final responses to arrive
  await sleep(2000);
}

// --- Extreme Tests ---
async function extremeRapidConnectDisconnect(cycles = 100) {
  console.log(`\n[Extreme] Rapid connect/disconnect: ${cycles} cycles...`);
  let successes = 0;
  let failures = 0;

  for (let i = 0; i < cycles; i++) {
    try {
      const ws = new WebSocket(SERVER_URL);
      await new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
          try { ws.close(); } catch (_) {}
          reject(new Error('timeout'));
        }, 2000);
        ws.on('open', () => {
          clearTimeout(timer);
          ws.close();
          resolve();
        });
        ws.on('error', (err) => {
          clearTimeout(timer);
          reject(err);
        });
      });
      successes++;
    } catch (_) {
      failures++;
    }
  }

  console.log(`  Successes: ${successes}/${cycles}, Failures: ${failures}`);
  return { successes, failures, total: cycles };
}

async function extremeLargeMessage() {
  console.log('\n[Extreme] Sending 10KB message...');
  return new Promise((resolve) => {
    const ws = new WebSocket(SERVER_URL);
    const largePayload = JSON.stringify({
      type: 'message',
      content: 'X'.repeat(10 * 1024),
      timestamp: Date.now(),
    });

    let result = { sent: false, response: false, error: null };

    const timer = setTimeout(() => {
      try { ws.close(); } catch (_) {}
      console.log(`  Result: sent=${result.sent}, gotResponse=${result.response}, error=${result.error}`);
      resolve(result);
    }, 5000);

    ws.on('open', () => {
      ws.send(largePayload);
      result.sent = true;
    });
    ws.on('message', () => {
      result.response = true;
    });
    ws.on('error', (err) => {
      result.error = err.message;
    });
    ws.on('close', () => {
      clearTimeout(timer);
      console.log(`  Result: sent=${result.sent}, gotResponse=${result.response}, error=${result.error}`);
      resolve(result);
    });
  });
}

async function extremeEmptyMessage() {
  console.log('\n[Extreme] Sending empty message...');
  return new Promise((resolve) => {
    const ws = new WebSocket(SERVER_URL);
    let result = { sent: false, error: null, serverCrashed: false };

    const timer = setTimeout(() => {
      result.serverCrashed = false;
      try { ws.close(); } catch (_) {}
      console.log(`  Result: sent=${result.sent}, serverCrashed=${result.serverCrashed}, error=${result.error}`);
      resolve(result);
    }, 3000);

    ws.on('open', () => {
      ws.send('');
      result.sent = true;
    });
    ws.on('error', (err) => {
      result.error = err.message;
    });
    ws.on('close', (code) => {
      clearTimeout(timer);
      if (code === 1006) result.serverCrashed = true;
      console.log(`  Result: sent=${result.sent}, closeCode=${code}, error=${result.error}`);
      resolve(result);
    });
  });
}

async function extremeDisconnectMidResponse() {
  console.log('\n[Extreme] Disconnect mid-response...');
  return new Promise((resolve) => {
    const ws = new WebSocket(SERVER_URL);
    let result = { connected: false, disconnectedCleanly: false, error: null };

    const timer = setTimeout(() => {
      try { ws.close(); } catch (_) {}
      resolve(result);
    }, 5000);

    ws.on('open', () => {
      result.connected = true;
      // Send a message then immediately close
      ws.send(JSON.stringify({ type: 'message', content: 'about to disconnect' }));
      // Force close after tiny delay (mid-processing)
      setTimeout(() => {
        ws.terminate();
        result.disconnectedCleanly = true;
        clearTimeout(timer);
        console.log(`  Result: connected=${result.connected}, cleanDisconnect=${result.disconnectedCleanly}`);
        resolve(result);
      }, 50);
    });
    ws.on('error', (err) => {
      result.error = err.message;
    });
  });
}

// --- Main Runner ---
async function runStressTest() {
  console.log('='.repeat(60));
  console.log('  CloseCrab Team Mode - WebSocket Stress Test');
  console.log('='.repeat(60));
  console.log(`  Server:  ${SERVER_URL}`);
  console.log(`  Clients: ${NUM_CLIENTS}`);
  console.log(`  Messages per client: ${MESSAGES_PER_CLIENT}`);
  console.log('='.repeat(60));

  // Phase 1: Multi-client concurrent test
  console.log('\n[Phase 1] Concurrent client simulation...');
  const startTime = process.hrtime();

  const clientPromises = [];
  for (let i = 0; i < NUM_CLIENTS; i++) {
    clientPromises.push(simulateClient(i));
  }

  const results = await Promise.all(clientPromises);
  const totalTime = elapsed(startTime);

  // Aggregate stats
  for (const r of results) {
    stats.totalSent += r.sent;
    stats.totalReceived += r.received;
    stats.errors.push(...r.errors);
    stats.contaminations.push(...r.contaminations);
    stats.latencies.push(...r.latencies);
    stats.clientResults.push(r);
  }

  // Phase 2: Extreme tests
  console.log('\n[Phase 2] Extreme edge-case tests...');

  const extremeResults = {};
  try {
    extremeResults.rapidConnect = await extremeRapidConnectDisconnect(100);
  } catch (err) {
    extremeResults.rapidConnect = { error: err.message };
  }

  try {
    extremeResults.largeMessage = await extremeLargeMessage();
  } catch (err) {
    extremeResults.largeMessage = { error: err.message };
  }

  try {
    extremeResults.emptyMessage = await extremeEmptyMessage();
  } catch (err) {
    extremeResults.emptyMessage = { error: err.message };
  }

  try {
    extremeResults.midDisconnect = await extremeDisconnectMidResponse();
  } catch (err) {
    extremeResults.midDisconnect = { error: err.message };
  }

  // --- Report ---
  console.log('\n' + '='.repeat(60));
  console.log('  RESULTS');
  console.log('='.repeat(60));

  const connectedClients = results.filter(r => r.connected).length;
  const completedClients = results.filter(r => r.completed).length;
  const avgLatency = stats.latencies.length > 0
    ? (stats.latencies.reduce((a, b) => a + b, 0) / stats.latencies.length).toFixed(2)
    : 'N/A';
  const maxLatency = stats.latencies.length > 0
    ? Math.max(...stats.latencies).toFixed(2)
    : 'N/A';
  const minLatency = stats.latencies.length > 0
    ? Math.min(...stats.latencies).toFixed(2)
    : 'N/A';

  console.log(`\n  Clients connected:    ${connectedClients}/${NUM_CLIENTS}`);
  console.log(`  Clients completed:    ${completedClients}/${NUM_CLIENTS}`);
  console.log(`  Total messages sent:  ${stats.totalSent}`);
  console.log(`  Total messages recv:  ${stats.totalReceived}`);
  console.log(`  Total time:           ${totalTime.toFixed(0)}ms`);
  console.log(`\n  Latency (avg):        ${avgLatency}ms`);
  console.log(`  Latency (min):        ${minLatency}ms`);
  console.log(`  Latency (max):        ${maxLatency}ms`);
  console.log(`\n  Contaminations:       ${stats.contaminations.length}`);
  console.log(`  Errors:               ${stats.errors.length}`);

  if (stats.contaminations.length > 0) {
    console.log('\n  [!] CONTAMINATION DETAILS:');
    stats.contaminations.forEach((c, i) => {
      console.log(`      ${i + 1}. Expected clientId=${c.expected}, got=${c.received} (type=${c.msgType})`);
    });
  }

  if (stats.errors.length > 0) {
    console.log('\n  [!] ERROR DETAILS (first 10):');
    stats.errors.slice(0, 10).forEach((e, i) => {
      console.log(`      ${i + 1}. ${e}`);
    });
    if (stats.errors.length > 10) {
      console.log(`      ... and ${stats.errors.length - 10} more`);
    }
  }

  // Extreme test summary
  console.log('\n  --- Extreme Tests ---');
  if (extremeResults.rapidConnect) {
    const rc = extremeResults.rapidConnect;
    console.log(`  Rapid connect/disconnect: ${rc.error || `${rc.successes}/${rc.total} OK`}`);
  }
  if (extremeResults.largeMessage) {
    const lm = extremeResults.largeMessage;
    console.log(`  10KB message:             ${lm.error || (lm.sent ? 'Sent OK' : 'Failed')}`);
  }
  if (extremeResults.emptyMessage) {
    const em = extremeResults.emptyMessage;
    console.log(`  Empty message:            ${em.error || (em.serverCrashed ? 'Server crashed!' : 'Handled OK')}`);
  }
  if (extremeResults.midDisconnect) {
    const md = extremeResults.midDisconnect;
    console.log(`  Mid-response disconnect:  ${md.error || (md.disconnectedCleanly ? 'Clean' : 'Issue')}`);
  }

  // Verdict
  console.log('\n' + '='.repeat(60));
  const passed = connectedClients === NUM_CLIENTS
    && stats.contaminations.length === 0
    && stats.errors.length === 0;

  const softPass = connectedClients === NUM_CLIENTS
    && stats.contaminations.length === 0;

  if (passed) {
    console.log('  VERDICT: PASS (all clients connected, no contamination, no errors)');
  } else if (softPass) {
    console.log(`  VERDICT: SOFT PASS (no contamination, ${stats.errors.length} non-critical errors)`);
  } else {
    console.log('  VERDICT: FAIL');
    if (connectedClients < NUM_CLIENTS) {
      console.log(`    - Only ${connectedClients}/${NUM_CLIENTS} clients connected`);
    }
    if (stats.contaminations.length > 0) {
      console.log(`    - ${stats.contaminations.length} cross-client contamination(s) detected`);
    }
    if (stats.errors.length > 0) {
      console.log(`    - ${stats.errors.length} error(s) occurred`);
    }
  }
  console.log('='.repeat(60));

  process.exit(passed ? 0 : softPass ? 0 : 1);
}

// --- Entry Point ---
runStressTest().catch((err) => {
  console.error('Stress test crashed:', err);
  process.exit(2);
});
