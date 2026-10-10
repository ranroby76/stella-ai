# Base44 prompt: Stella AI Cloud, version 5 (builder at extra-high effort)

Open the **Stella AI Cloud** app on Base44 and paste everything below the line into its AI chat. It only changes two settings in the `stellaChat` function; everything else stays as it is.

---

Update this app: make two small changes to the backend function `stellaChat`, and change nothing else.

1. In the `MODES` table, give the `build` mode an effort level of `'xhigh'` and raise its `maxTokens` from 12000 to 24000. The `chat` mode stays exactly as it is (no effort level). The table becomes:

```ts
const MODES: Record<string, { model: string; maxTokens: number; priceWeight: number; effort?: string }> = {
  chat:  { model: 'claude-sonnet-5-5', maxTokens: 4096,  priceWeight: 1 },
  build: { model: 'claude-opus-5-5',   maxTokens: 24000, priceWeight: 2, effort: 'xhigh' },   // deeper thinking for writing and fixing code; 24000 leaves room for it and the code
};
```

2. Where the request to the AI service is put together, right after the line that adds the tools (`if (Array.isArray(body.tools) && body.tools.length > 0) request.tools = body.tools;`), add:

```ts
    if (setup.effort) request.output_config = { effort: setup.effort };
```

Make sure that:

- Nothing else in the function changes: the credit charging, the wallets, the message cleaning, the reply format and the error handling all stay as they are.
- The function still works without a signed-in user (the studio calls it).

Also tell me: **what's the longest a backend function may run here?** A building step can now take up to about 5 minutes. If there's a setting for it, raise it to the maximum.

When you're done, show me the new `MODES` table and the line you added.
