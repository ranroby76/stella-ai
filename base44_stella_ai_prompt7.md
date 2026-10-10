# Base44 prompt: Stella AI Cloud, version 7 (builder at high effort)

Open the **Stella AI Cloud** app on Base44 and paste everything below the line into its AI chat. It changes one line of the `stellaChat` function.

---

Update the backend function `stellaChat`: in the `MODES` table, change the `build` mode's effort from `'xhigh'` to `'high'` and its `maxTokens` from 16000 to 20000. Change nothing else (keep `secrets.get` for the key, the deadline, the credit charging and the reply format exactly as they are). The table becomes:

```ts
const MODES: Record<string, { model: string; maxTokens: number; priceWeight: number; effort?: string }> = {
  chat:  { model: 'claude-sonnet-5-5', maxTokens: 4096,  priceWeight: 1 },
  build: { model: 'claude-opus-5-5',   maxTokens: 20000, priceWeight: 2, effort: 'high' },   // extra-high thinking doesn't fit a step inside Base44's 5 minutes
};
```

When you're done, show me the new `MODES` table.
