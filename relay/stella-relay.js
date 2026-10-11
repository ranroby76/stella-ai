// C:\workspace\Stella AI Studio\relay\stella-relay.js
//
// Stella AI's relay: a Cloudflare Worker that makes the AI call for a building step, so a long
// step isn't cut off by Base44's time limit (about 2 minutes). Base44 still does everything
// else: it checks the computer or account and its credits, builds the request, charges the
// credits and makes the reply. The studio sends the relay exactly what it would send Base44's
// stellaChat, and gets back exactly what stellaChat would answer.
//
//   1. prepare  Base44 checks the credits and returns the request to send (no AI call there).
//   2. call     the relay sends it to the AI service with the key, streamed, with no time limit.
//   3. finish   Base44 charges the credits from the answer's usage and makes the reply.
//
// While it works, the relay sends a space every few seconds (JSON allows them before the
// answer), so no network hop drops the connection for being quiet. The answer is always sent
// with status 200; a failure is { ok: false, error, message }, as from Base44.
//
// Set up in the Cloudflare dashboard (Workers & Pages > this worker > Settings > Variables
// and Secrets), as secrets:
//   AI_KEY        the AI service key (the same one Base44 has)
//   RELAY_SECRET  a long random text, the same as Base44's RELAY_SECRET secret
// Optional plain variables: BASE44_URL and APP_ID, if they ever change.
//
// Messages never name the AI service; the details go to the worker's logs only.

const BASE44_URL = 'https://base44.app';
const APP_ID = '6ac03f5bd8e7cddad8252682';

const DEADLINE_MS = 10 * 60 * 1000;   // a step that runs longer than this is given up (the studio waits 11 minutes)
const KEEP_ALIVE_MS = 8000;

const MESSAGES = {
  failed: "Stella AI couldn't answer that, and nothing was charged. Please try again.",
  busy: 'Stella AI is busy right now, and nothing was charged. Please try again in a moment.',
  timeout: 'That step took too long, and nothing was charged. Please try again.',
  unavailable: "Stella AI isn't available right now, and nothing was charged. Please try again later.",
};

// A failure the studio shows as it is; the code after the message helps find the cause.
function failure(kind, code) {
  return { ok: false, error: kind, message: `${MESSAGES[kind] || MESSAGES.failed} (${code})` };
}

function describe(error) {
  return String((error && (error.stack || error.message)) || error);
}

//==============================================================================
export default {
  async fetch(request, env, ctx) {
    if (request.method !== 'POST')
      return new Response('Stella AI relay', { status: 200, headers: { 'content-type': 'text/plain' } });

    const text = await request.text();
    const { readable, writable } = new TransformStream();
    const writer = writable.getWriter();
    const encoder = new TextEncoder();

    const send = (piece) => writer.write(encoder.encode(piece)).catch(() => {});
    send(' ');
    const keepAlive = setInterval(() => send(' '), KEEP_ALIVE_MS);

    const work = (async () => {
      let result;

      try {
        result = await handle(text, env);
      } catch (error) {
        console.error('Relay failed', describe(error));
        result = failure('failed', 'relay');
      }

      clearInterval(keepAlive);
      await send(JSON.stringify(result));
      await writer.close().catch(() => {});
    })();

    ctx.waitUntil(work);

    return new Response(readable, {
      status: 200,
      headers: { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store' },
    });
  },
};

//==============================================================================
async function handle(text, env) {
  let request;

  try {
    request = JSON.parse(text);
  } catch {
    return failure('failed', 'request');
  }

  if (!request || typeof request !== 'object' || Array.isArray(request))
    return failure('failed', 'request');

  if (!env.AI_KEY || !env.RELAY_SECRET) {
    console.error('The relay is missing its AI_KEY or RELAY_SECRET secret');
    return failure('unavailable', 'relay-setup');
  }

  // 1. Base44 checks the computer or account and its credits, and builds the request.
  const prepared = await base44(env, { ...request, relayAction: 'prepare', relaySecret: env.RELAY_SECRET });

  if (!prepared.json) {
    console.error('prepare: no answer', prepared.status, prepared.text.slice(0, 300));
    return failure('failed', `prepare-${prepared.status}`);
  }

  // Its own answer as it is: out of credits and the like (or, from a Base44 that doesn't know
  // the relay yet, the finished reply).
  if (!prepared.json.ok || !prepared.json.call)
    return prepared.json;

  // 2. The AI call, streamed, with the key.
  const { url, headers = {}, body } = prepared.json.call;

  if (!url || !body) {
    console.error('prepare: no call in the answer');
    return failure('failed', 'prepare-call');
  }

  const sent = new Headers();

  for (const [name, value] of Object.entries(headers)) {
    const lower = name.toLowerCase();

    if (!['x-api-key', 'authorization', 'content-length', 'host'].includes(lower))
      sent.set(name, String(value));
  }

  sent.set('x-api-key', env.AI_KEY);
  sent.set('content-type', 'application/json');

  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), DEADLINE_MS);
  let data;

  try {
    const answer = await fetch(url, {
      method: 'POST',
      headers: sent,
      body: JSON.stringify({ ...body, stream: true }),
      signal: controller.signal,
    });

    if (!answer.ok) {
      const detail = await answer.text().catch(() => '');
      console.error('AI service error', answer.status, detail.slice(0, 600));
      return serviceFailure(answer.status, detail);
    }

    data = await readStream(answer);
  } catch (error) {
    if (controller.signal.aborted) {
      console.error('AI service step went past the deadline');
      return failure('timeout', 'deadline');
    }

    if (error && error.streamError) {
      console.error('AI service stream error', JSON.stringify(error.streamError));
      return failure('busy', 'stream');
    }

    console.error('AI service call failed', describe(error));
    return failure('failed', 'service-call');
  } finally {
    clearTimeout(timer);
  }

  // 3. Base44 charges the credits and makes the reply. Tried again only when it couldn't be
  // reached at all (an answer, even a refusal, is passed on as it is).
  for (let attempt = 1; attempt <= 3; ++attempt) {
    const finished = await base44(env, { ...request, relayAction: 'finish', relaySecret: env.RELAY_SECRET, data });

    if (finished.json)
      return finished.json;

    console.error('finish: no answer', attempt, finished.status, finished.text.slice(0, 300));
    await new Promise((resolve) => setTimeout(resolve, 1500 * attempt));
  }

  return failure('failed', 'finish');
}

/** Base44's stellaChat, as the studio calls it. json is null if the answer isn't JSON. */
async function base44(env, body) {
  const appId = env.APP_ID || APP_ID;
  const address = `${(env.BASE44_URL || BASE44_URL).replace(/\/+$/, '')}/api/apps/${appId}/functions/stellaChat`;

  try {
    const response = await fetch(address, {
      method: 'POST',
      headers: { 'Content-Type': 'application/json', 'Accept': 'application/json', 'X-App-Id': appId },
      body: JSON.stringify(body),
    });

    const text = await response.text();

    try {
      const json = JSON.parse(text);
      return { status: response.status, json: json && typeof json === 'object' ? json : null, text };
    } catch {
      return { status: response.status, json: null, text };
    }
  } catch (error) {
    return { status: 0, json: null, text: describe(error) };
  }
}

/** The AI service said no: what the user is told. */
function serviceFailure(status, detail) {
  const lower = detail.toLowerCase();

  if (status === 429 || status === 529 || lower.includes('overloaded') || lower.includes('rate_limit'))
    return failure('busy', `service-${status}`);

  if (status === 401 || status === 403 || lower.includes('credit balance') || lower.includes('billing'))
    return failure('unavailable', `service-${status}`);

  if (status === 413 || lower.includes('too long') || lower.includes('too large'))
    return failure('failed', 'too-long');

  return failure('failed', `service-${status}`);
}

//==============================================================================
// The streamed answer put back together into the shape of a plain one: { content, stop_reason,
// usage }, with the text and tool-call blocks (as Base44 did it before the relay).
async function readStream(response) {
  const usage = {};
  const blocks = [];
  let stopReason = null;
  let finished = false;
  let buffer = '';

  const handleEvent = (event) => {
    switch (event.type) {
      case 'message_start':
        Object.assign(usage, (event.message && event.message.usage) || {});
        break;

      case 'content_block_start': {
        const b = event.content_block || {};
        blocks[event.index] = b.type === 'tool_use' ? { type: 'tool_use', id: b.id, name: b.name, json: '' }
                            : b.type === 'text' ? { type: 'text', text: b.text || '' }
                            : { type: 'other' };
        break;
      }

      case 'content_block_delta': {
        const b = blocks[event.index];
        const delta = event.delta || {};
        if (b && delta.type === 'text_delta') b.text += delta.text || '';
        if (b && delta.type === 'input_json_delta') b.json += delta.partial_json || '';
        break;
      }

      case 'message_delta':
        if (event.delta && event.delta.stop_reason) stopReason = event.delta.stop_reason;
        Object.assign(usage, event.usage || {});
        break;

      case 'message_stop':
        finished = true;
        break;

      case 'error':
        throw Object.assign(new Error((event.error && event.error.message) || 'stream error'), { streamError: event.error || {} });
    }
  };

  const reader = response.body.pipeThrough(new TextDecoderStream()).getReader();

  for (;;) {
    const { value, done } = await reader.read();

    if (done)
      break;

    buffer += (value || '').replace(/\r/g, '');

    let end;

    while ((end = buffer.indexOf('\n\n')) >= 0) {
      const chunk = buffer.slice(0, end);
      buffer = buffer.slice(end + 2);

      const payload = chunk.split('\n')
                           .filter((line) => line.startsWith('data:'))
                           .map((line) => line.slice(5).trim())
                           .join('');

      if (payload)
        handleEvent(JSON.parse(payload));
    }
  }

  // A stream that stopped half-way is a failure: nothing is charged for it.
  if (!finished)
    throw Object.assign(new Error('the stream ended early'), { streamError: { type: 'cut_off' } });

  // A tool call cut off half-way (the answer ran out of room) is left out.
  const content = blocks.filter(Boolean).flatMap((b) => {
    if (b.type === 'text')
      return b.text ? [{ type: 'text', text: b.text }] : [];

    if (b.type !== 'tool_use')
      return [];

    try {
      return [{ type: 'tool_use', id: b.id, name: b.name, input: b.json ? JSON.parse(b.json) : {} }];
    } catch {
      return [];
    }
  });

  return { content, stop_reason: stopReason, usage };
}
