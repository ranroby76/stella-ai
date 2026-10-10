# Base44 prompt: Stella AI Cloud, version 9 (streamed answers, so long steps aren't dropped)

Open the **Stella AI Cloud** app on Base44 and paste everything below the line into its AI chat. It changes the `stellaChat` function only, and it includes version 8 (if 8 was already pasted, nothing is lost).

---

Update the backend function `stellaChat`. The studio's log shows building steps that take longer than about 125 seconds always fail with a 500, while shorter ones work: the connection to the AI service is dropped when no data flows for that long, because the answer only arrives at the end. The fix is to ask the AI service to stream its answer (data then flows the whole time) and put the streamed pieces back together into the same answer as before. Make these changes and nothing else:

1. The `MODES` table must be exactly this (the `chat` line gets room for its thinking):

```ts
const MODES: Record<string, { model: string; maxTokens: number; priceWeight: number; effort?: string }> = {
  chat:  { model: 'claude-sonnet-5-5', maxTokens: 16000, priceWeight: 1, effort: 'medium' },   // its thinking counts toward maxTokens: 4096 cut answers off
  build: { model: 'claude-opus-5-5',   maxTokens: 20000, priceWeight: 2, effort: 'high' },     // extra-high thinking doesn't fit a step inside Base44's 5 minutes
};
```

2. Add this helper function at the top level of the file, next to the other helpers (for example right after `cleanMessages`):

```ts
// A streamed answer keeps the connection busy, so no network hop drops a long step for being
// quiet. This puts the stream's events back together into the shape of a plain answer:
// { content, stop_reason, usage }, with only the text and tool-call blocks.
async function readStream(response: Response): Promise<any> {
  const usage: any = {};
  const blocks: any[] = [];
  let stopReason: string | null = null;
  let buffer = '';

  const handle = (event: any) => {
    switch (event.type) {
      case 'message_start':
        Object.assign(usage, event.message?.usage || {});
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
        if (b && event.delta?.type === 'text_delta') b.text += event.delta.text || '';
        if (b && event.delta?.type === 'input_json_delta') b.json += event.delta.partial_json || '';
        break;
      }
      case 'message_delta':
        if (event.delta?.stop_reason) stopReason = event.delta.stop_reason;
        Object.assign(usage, event.usage || {});
        break;
      case 'error':
        throw Object.assign(new Error(event.error?.message || 'stream error'), { streamError: event.error || {} });
    }
  };

  const reader = response.body!.pipeThrough(new TextDecoderStream()).getReader();

  for (;;) {
    const { value, done } = await reader.read();
    if (done) break;
    buffer += (value || '').replace(/\r/g, '');

    let end;
    while ((end = buffer.indexOf('\n\n')) >= 0) {
      const chunk = buffer.slice(0, end);
      buffer = buffer.slice(end + 2);
      const payload = chunk.split('\n').filter((line) => line.startsWith('data:')).map((line) => line.slice(5).trim()).join('');
      if (payload) handle(JSON.parse(payload));
    }
  }

  // A tool call cut off half-way (the answer ran out of room) is left out.
  const content = blocks.filter(Boolean).flatMap((b: any) => {
    if (b.type === 'text') return b.text ? [{ type: 'text', text: b.text }] : [];
    if (b.type !== 'tool_use') return [];
    try {
      return [{ type: 'tool_use', id: b.id, name: b.name, input: b.json ? JSON.parse(b.json) : {} }];
    } catch {
      return [];
    }
  });

  return { content, stop_reason: stopReason, usage };
}
```

3. In the part that calls the AI service, inside the existing `try`, make two changes and keep everything else (the deadline, `secrets.get` for the key, the headers):
   - the body becomes `body: JSON.stringify({ ...request, stream: true }),`
   - the line `data = await answer.json();` becomes `data = answer.ok ? await readStream(answer) : await answer.json();`

4. In the existing `catch (error)` of that same block, right after the `if (controller.signal.aborted) { ... }` part and before `throw error;`, add:

```ts
      if ((error as any)?.streamError) {
        console.error('AI service stream error', JSON.stringify((error as any).streamError));   // in your logs only
        return Response.json({
          ok: false,
          error: 'busy',
          message: 'Stella AI is busy right now, and nothing was charged. Please try again in a moment.',
        }, { status: 502 });
      }
```

Make sure that:

- Everything after the call stays exactly as it is: the `if (!answer.ok)` error handling, the credit charging (it reads `data.usage`), the wallets, the reply format (it reads `data.content` and `data.stop_reason`).
- The function still works without a signed-in user (the studio calls it).

When you're done, show me the `MODES` table and the new code of the AI service call, `try` to `finally`.
