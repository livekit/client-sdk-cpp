/*
 * Copyright 2026 LiveKit, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * Zero-dependency HTTP fixture for TokenSource endpoint integration tests.
 *
 * Usage:
 *   node src/tests/fixtures/token_server.mjs [port]
 *
 * Exports LIVEKIT_TOKEN_FIXTURE_URL=http://127.0.0.1:<port>
 */

import http from 'node:http';

const VALID_TOKEN = 'eyJhbGciOiJub25lIn0.eyJleHAiOjk5OTk5OTk5OTk5fQ.';
const SERVER_URL = 'wss://fixture.livekit.test';

function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on('data', (chunk) => chunks.push(chunk));
    req.on('end', () => resolve(Buffer.concat(chunks).toString('utf8')));
    req.on('error', reject);
  });
}

function parseJson(body) {
  if (!body || body.trim() === '') {
    return {};
  }
  return JSON.parse(body);
}

function sendJson(res, statusCode, payload) {
  const body = JSON.stringify(payload);
  res.writeHead(statusCode, { 'Content-Type': 'application/json' });
  res.end(body);
}

function sendText(res, statusCode, body) {
  res.writeHead(statusCode, { 'Content-Type': 'text/plain' });
  res.end(body);
}

async function handleRequest(req, res) {
  const url = new URL(req.url ?? '/', `http://${req.headers.host ?? '127.0.0.1'}`);
  const path = url.pathname;
  const method = req.method ?? 'GET';

  let bodyStr = '';
  try {
    bodyStr = await readBody(req);
  } catch {
    sendText(res, 400, 'failed to read body');
    return;
  }

  let body = {};
  if (bodyStr.length > 0 && path !== '/malformed') {
    try {
      body = parseJson(bodyStr);
    } catch {
      sendText(res, 400, 'invalid json');
      return;
    }
  }

  if (path === '/snake') {
    if (method !== 'POST') {
      sendText(res, 405, 'method not allowed');
      return;
    }
    if (body.room_name === 'my-room' && body.agent_name === 'assistant') {
      if (!body.room_config?.agents?.[0]?.agent_name) {
        sendText(res, 400, 'missing room_config.agents');
        return;
      }
    }
    sendJson(res, 200, {
      server_url: SERVER_URL,
      participant_token: VALID_TOKEN,
      room_name: body.room_name ?? 'default-room',
      participant_name: body.participant_name ?? 'default-participant',
    });
    return;
  }

  if (path === '/camel') {
    if (method !== 'POST') {
      sendText(res, 405, 'method not allowed');
      return;
    }
    sendJson(res, 200, {
      serverUrl: SERVER_URL,
      participantToken: VALID_TOKEN,
      roomName: 'room-name',
      participantName: 'participant-name',
    });
    return;
  }

  if (path === '/forbidden') {
    sendJson(res, 403, { error: 'forbidden' });
    return;
  }

  if (path === '/malformed') {
    sendText(res, 200, 'this-is-not-json');
    return;
  }

  if (path === '/get-only') {
    if (method !== 'GET') {
      sendText(res, 405, 'GET required');
      return;
    }
    sendJson(res, 200, {
      server_url: SERVER_URL,
      participant_token: VALID_TOKEN,
    });
    return;
  }

  if (path === '/headers') {
    if (method !== 'POST') {
      sendText(res, 405, 'method not allowed');
      return;
    }
    const auth = req.headers.authorization;
    const custom = req.headers['x-custom'];
    if (auth !== 'Bearer my-token' || custom !== 'value') {
      sendText(res, 400, 'missing or incorrect headers');
      return;
    }
    sendJson(res, 200, {
      server_url: SERVER_URL,
      participant_token: VALID_TOKEN,
    });
    return;
  }

  if (path === '/api/v2/sandbox/connection-details') {
    if (method !== 'POST') {
      sendText(res, 405, 'method not allowed');
      return;
    }
    const sandboxId = req.headers['x-sandbox-id'];
    if (sandboxId !== 'sandbox-123') {
      sendText(res, 400, `unexpected X-Sandbox-ID: ${sandboxId ?? '(missing)'}`);
      return;
    }
    sendJson(res, 200, {
      server_url: SERVER_URL,
      participant_token: VALID_TOKEN,
    });
    return;
  }

  sendText(res, 404, 'not found');
}

const port = Number(process.argv[2] ?? process.env.TOKEN_FIXTURE_PORT ?? '9876');
const server = http.createServer((req, res) => {
  handleRequest(req, res).catch((err) => {
    console.error(err);
    sendText(res, 500, 'internal error');
  });
});

server.listen(port, '127.0.0.1', () => {
  console.log(`token fixture listening on http://127.0.0.1:${port}`);
});
