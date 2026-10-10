# Base44 prompt: Stella AI Cloud, version 8 (room for the conversation's thinking)

Open the **Stella AI Cloud** app on Base44 and paste everything below the line into its AI chat. It changes one line of the `stellaChat` function.

---

Update the backend function `stellaChat`: in the `MODES` table, give the `chat` mode an effort of `'medium'` and raise its `maxTokens` from 4096 to 16000. The `build` line stays as it is. Change nothing else (keep `secrets.get` for the key, the deadline, the credit charging and the reply format exactly as they are). The table becomes:

```ts
const MODES: Record<string, { model: string; maxTokens: number; priceWeight: number; effort?: string }> = {
  chat:  { model: 'claude-sonnet-5-5', maxTokens: 16000, priceWeight: 1, effort: 'medium' },   // its thinking counts toward maxTokens: 4096 cut answers off
  build: { model: 'claude-opus-5-5',   maxTokens: 20000, priceWeight: 2, effort: 'high' },     // extra-high thinking doesn't fit a step inside Base44's 5 minutes
};
```

Then show me the last 20 lines of this function's logs that contain "AI service error" or "deadline", if there are any, word for word.

When you're done, show me the new `MODES` table.
