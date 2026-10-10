# Base44 prompt: Stella AI Cloud, version 6 (builder steps that fit in 5 minutes)

Open the **Stella AI Cloud** app on Base44 and paste everything below the line into its AI chat. It changes the `stellaChat` function only.

---

Update the backend function `stellaChat` so that a building step always finishes inside the 5-minute limit, with a clear answer instead of being stopped. Two changes, nothing else:

1. In the `MODES` table, lower the `build` mode's `maxTokens` from 24000 to 16000. Keep `effort: 'xhigh'`. The table becomes:

```ts
const MODES: Record<string, { model: string; maxTokens: number; priceWeight: number; effort?: string }> = {
  chat:  { model: 'claude-sonnet-5-5', maxTokens: 4096,  priceWeight: 1 },
  build: { model: 'claude-opus-5-5',   maxTokens: 16000, priceWeight: 2, effort: 'xhigh' },   // deeper thinking; 16000 keeps a step inside Base44's 5 minutes
};
```

2. Give the call to the AI service a deadline a little under the platform's limit. Add this constant next to the other constants at the top:

```ts
const DEADLINE_MS = 270000;   // Base44 stops a function at 5 minutes: give up a little before, with an answer the studio can show
```

Then replace the part that calls the AI service and reads its answer (the `fetch('https://api.anthropic.com/v1/messages', ...)` call and the `answer.json()` after it) with this, keeping the same headers and body:

```ts
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), DEADLINE_MS);
    let answer: Response;
    let data: any;

    try {
      answer = await fetch('https://api.anthropic.com/v1/messages', {
        method: 'POST',
        headers: {
          'x-api-key': Deno.env.get('ANTHROPIC_API_KEY') ?? '',
          'anthropic-version': '2023-06-01',
          'content-type': 'application/json',
        },
        body: JSON.stringify(request),
        signal: controller.signal,
      });
      data = await answer.json();
    } catch (error) {
      if (controller.signal.aborted) {
        console.error('AI service step went past the deadline');   // in your logs only
        return Response.json({
          ok: false,
          error: 'busy',
          message: 'That step took too long, and nothing was charged. Please send it again, or ask for a smaller change.',
        }, { status: 504 });
      }
      throw error;
    } finally {
      clearTimeout(timer);
    }
```

Make sure that:

- The rest stays exactly as it is: the error handling right after (`if (!answer.ok) ...`), the credit charging, the wallets, the message cleaning and the reply format.
- A step that runs out of time charges no credits (it returns before the charging code).
- The function still works without a signed-in user (the studio calls it).

When you're done, show me the new `MODES` table and the new code around the AI service call.
