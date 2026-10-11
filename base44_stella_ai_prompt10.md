# Base44 prompt: Stella AI Cloud, version 10 (the relay: long steps run outside Base44)

First, in the **Stella AI Cloud** app on Base44, add two secrets (where the AI key secret is kept):

- `RELAY_SECRET`: the same long random text you gave the relay's `RELAY_SECRET` on Cloudflare
- `RELAY_URL`: the relay's address on Cloudflare, e.g. `https://stella-relay.yourname.workers.dev`

Then paste everything below the line into the app's AI chat. It changes `stellaChat` and the `hello` reply of `stellaAccount` (paste version 9 first if it isn't in yet).

---

Building steps that take longer than about 2 minutes fail on Base44, so from now on a relay server outside Base44 makes the AI service call. Base44 keeps doing everything else: checking the computer or account and its credits, building the request, charging the credits and making the reply. The relay calls `stellaChat` twice per step, with the studio's request plus `relayAction` and `relaySecret`: once to prepare the call, once to finish it with the AI service's answer. There are two new secrets, `RELAY_SECRET` and `RELAY_URL`; read them the same way the function reads the AI key now. Make these changes and nothing else:

1. In `stellaChat`, right after the request body is read: if the body has `relayAction`, its `relaySecret` must equal the secret `RELAY_SECRET`. If `RELAY_SECRET` isn't set, or `relaySecret` is missing or different, return `{ ok: false, error: 'forbidden', message: "Stella AI couldn't answer that. Please try again." }` with status 403 and do nothing else. `relaySecret` is never stored, logged or sent back.

2. `relayAction: 'prepare'`: run the function exactly as now (the same checks of the token, computer, wallet and credits, with the same refusals, e.g. out of credits) up to the point where it calls the AI service. Instead of calling it, return status 200 with:

```ts
{ ok: true, call: { url, headers, body } }
```

   - `url`: the AI service address it calls now
   - `headers`: every header it sends now, except the one carrying the AI key
   - `body`: the exact request object it sends now (model, max_tokens, system, messages, tools, thinking and everything else from `MODES`), without `stream`

   Nothing is charged and nothing in the database changes.

3. `relayAction: 'finish'`: the body also carries `data`, the AI service's answer already put together, `{ content, stop_reason, usage }` (the same shape `readStream` makes now). Run the same checks to find the computer and wallet (but don't refuse because the credits ran out meanwhile: this step was already allowed), skip the AI call, use `data = body.data`, and run all the code that follows the call now, unchanged: the credit charging (from `data.usage` and the mode's `priceWeight`), the token counts, the wallet and computer updates, and the reply `{ ok, reply, content, stop, used, credits }`.

4. A body without `relayAction` works exactly as before, with the AI call in place (the studio falls back to it if the relay can't be reached).

5. In `stellaAccount`, the reply to `action: 'hello'` (when it's ok) also carries `relay`: the value of the secret `RELAY_URL`, or an empty string if it isn't set. The studio sends Stella AI's requests there when it's set.

Make sure that:

- `MODES`, the credit charging, the wallets, the PayPal functions and the reply format stay exactly as they are.
- Both functions still work without a signed-in user (the studio and the relay call them).

When you're done, show me the new part of `stellaChat` from reading the body to the end of the `relayAction` handling, and the lines of `stellaAccount` that build the `hello` reply.
