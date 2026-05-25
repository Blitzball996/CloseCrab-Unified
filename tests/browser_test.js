const { chromium } = require('playwright');

const SERVER_URL = process.argv[2] || 'http://localhost:3000';
const NUM_CLIENTS = parseInt(process.argv[3] || '4');

async function runBrowserTest() {
  console.log(`\n=== CloseCrab Team Mode Browser Test ===`);
  console.log(`Server: ${SERVER_URL}`);
  console.log(`Clients: ${NUM_CLIENTS}\n`);

  const browser = await chromium.launch({ headless: true });
  const contexts = [];
  const pages = [];

  try {
    // Phase 1: Open multiple browser tabs
    console.log('[Phase 1] Opening browser contexts...');
    for (let i = 0; i < NUM_CLIENTS; i++) {
      const ctx = await browser.newContext();
      const page = await ctx.newPage();
      contexts.push(ctx);
      pages.push(page);
    }

    // Phase 2: Navigate to CloseCrab-Web
    console.log('[Phase 2] Navigating to CloseCrab-Web...');
    const results = await Promise.all(pages.map(async (page, i) => {
      try {
        await page.goto(SERVER_URL, { timeout: 10000 });
        const title = await page.title();
        return { idx: i, ok: true, title };
      } catch (e) {
        return { idx: i, ok: false, error: e.message };
      }
    }));

    let passed = 0;
    for (const r of results) {
      if (r.ok) {
        console.log(`  Client ${r.idx}: loaded (title: "${r.title}")`);
        passed++;
      } else {
        console.log(`  Client ${r.idx}: FAILED - ${r.error}`);
      }
    }

    // Phase 3: Check Team tab exists
    console.log('\n[Phase 3] Checking Team tab...');
    const teamBtnExists = await pages[0].locator('#btn-team, [onclick*="team"], button:has-text("Team")').count();
    console.log(`  Team button found: ${teamBtnExists > 0 ? 'YES' : 'NO'}`);

    // Phase 4: Test API endpoints from browser
    console.log('\n[Phase 4] Testing API from browser context...');
    const leaderboard = await pages[0].evaluate(async () => {
      const res = await fetch('/api/leaderboard');
      return await res.json();
    });
    console.log(`  /api/leaderboard: ${leaderboard.length} entries`);
    leaderboard.forEach((e, i) => console.log(`    ${i + 1}. ${e.username} (${e.score} pts)`));

    const clients = await pages[0].evaluate(async () => {
      const res = await fetch('/api/clients');
      return await res.json();
    });
    console.log(`  /api/clients: ${clients.length} connected`);

    // Phase 5: Test WebSocket connection from multiple pages
    console.log('\n[Phase 5] Testing WebSocket per-client isolation...');
    const wsResults = await Promise.all(pages.map(async (page, i) => {
      return await page.evaluate(async (idx) => {
        return new Promise((resolve) => {
          const proto = location.protocol === 'https:' ? 'wss:' : 'ws:';
          // First create a session
          fetch('/api/sessions', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: '{}' })
            .then(r => r.json())
            .then(session => {
              const ws = new WebSocket(`${proto}//${location.host}/?session=${session.id}`);
              let clientId = null;
              ws.onmessage = (e) => {
                try {
                  const msg = JSON.parse(e.data);
                  if (msg.type === 'connected') {
                    clientId = msg.clientId;
                    ws.send(JSON.stringify({ type: 'register', username: 'BrowserUser' + idx }));
                    setTimeout(() => {
                      ws.close();
                      resolve({ idx, clientId, ok: true });
                    }, 500);
                  }
                } catch {}
              };
              ws.onerror = () => resolve({ idx, clientId: null, ok: false, error: 'ws error' });
              setTimeout(() => {
                ws.close();
                resolve({ idx, clientId, ok: clientId !== null });
              }, 3000);
            })
            .catch(e => resolve({ idx, clientId: null, ok: false, error: e.message }));
        });
      }, i);
    }));

    const clientIds = new Set();
    let wsPass = 0;
    for (const r of wsResults) {
      if (r.ok && r.clientId) {
        console.log(`  Client ${r.idx}: clientId=${r.clientId.slice(0, 8)}...`);
        clientIds.add(r.clientId);
        wsPass++;
      } else {
        console.log(`  Client ${r.idx}: FAILED - ${r.error || 'no clientId'}`);
      }
    }

    const uniqueIds = clientIds.size === wsPass;
    console.log(`  Unique clientIds: ${clientIds.size}/${wsPass} ${uniqueIds ? '(PASS)' : '(FAIL - duplicates!)'}`);

    // Phase 6: Verify clients show up in API
    console.log('\n[Phase 6] Verifying clients registered...');
    await new Promise(r => setTimeout(r, 1000));
    const finalClients = await pages[0].evaluate(async () => {
      const res = await fetch('/api/clients');
      return await res.json();
    });
    console.log(`  Registered clients: ${finalClients.length}`);
    finalClients.forEach(c => console.log(`    - ${c.username} (${c.clientId.slice(0, 8)}...)`));

    // Summary
    console.log('\n=== RESULTS ===');
    console.log(`Pages loaded: ${passed}/${NUM_CLIENTS}`);
    console.log(`WebSocket connected: ${wsPass}/${NUM_CLIENTS}`);
    console.log(`Unique IDs: ${uniqueIds ? 'PASS' : 'FAIL'}`);
    console.log(`Team API: ${leaderboard.length > 0 ? 'PASS' : 'FAIL'}`);

    const allPass = passed === NUM_CLIENTS && wsPass === NUM_CLIENTS && uniqueIds;
    console.log(`\nVERDICT: ${allPass ? 'ALL TESTS PASSED' : 'SOME TESTS FAILED'}\n`);
    return allPass ? 0 : 1;

  } finally {
    for (const ctx of contexts) await ctx.close();
    await browser.close();
  }
}

runBrowserTest()
  .then(code => process.exit(code))
  .catch(e => { console.error('Fatal:', e); process.exit(2); });
