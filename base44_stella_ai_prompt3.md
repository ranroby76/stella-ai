# Base44 prompt: Stella AI Cloud, version 3 (the builder)

Open the **Stella AI Cloud** app on Base44 and paste everything below the line into its AI chat. It only replaces the `stellaChat` function; everything else stays as it is.

---

Update this app: replace the code of the backend function `stellaChat` with exactly the code below. Keep the same two small platform changes you made to it before (the opening import line, and reading the AI service key from the app's secrets). It must still work without a signed-in user (the studio calls it). Nothing else in the app changes.

What's new: Stella AI now builds plugins with the studio's tools. The studio sends a mode ("chat" for conversation, "build" for writing and fixing a plugin's code, which uses the stronger model and costs twice the credits), a long builder guide (the same every time, so it's cached), the project's details, and the tool definitions. The reply also says when an answer was cut off.

Also tell me: **what's the longest a backend function may run here?** One building step can take up to about 3 minutes. If there's a setting for it, raise it to the maximum.

```ts
import { createClientFromRequest } from 'npm:@base44/sdk';

// Stella AI: the studio's requests come here. This function asks the AI service with Fanan's
// key (which never leaves the server), charges the account's credits (or the computer's own,
// if it isn't signed in), and answers in Fanan's own format: nothing names the service behind it.
//
// Two modes: "chat" (conversation and questions) and "build" (writing and fixing a plugin's code,
// with the strongest model). The studio says which; a conversation hands over to the builder
// through its start_building tool.
const MODES: Record<string, { model: string; maxTokens: number; priceWeight: number }> = {
  chat:  { model: 'claude-sonnet-5-5', maxTokens: 4096,  priceWeight: 1 },
  build: { model: 'claude-opus-5-5',   maxTokens: 12000, priceWeight: 2 },   // costs twice as much; 12000 keeps a step well under Base44's 5 minutes
};
const MAX_MESSAGES = 120;
const OUTPUT_WEIGHT = 5;           // an output token costs about five input tokens
const TOKENS_PER_CREDIT = 10000;   // weighted tokens per credit: tune this to your prices

const PERSONA = `You are Stella AI, the assistant inside Stella AI Studio, a plugin-making studio by Fanan.
You build audio plugins (instruments, audio effects and MIDI effects) for the user with the studio's tools, and they play live in the studio while you work.
Act first: when the user asks for a plugin or a change, start building right away with sensible defaults, then say briefly what you made and offer one or two follow-ups. Ask a question only when you truly can't proceed without the answer. Keep replies short, and never paste code into a reply.
Don't discuss which AI model, company or service powers you. If asked, say you're Stella AI, Fanan's assistant, and that you can't share details about the technology behind it. Never claim to be a different specific model.`;

const HEX64 = /^[0-9a-f]{64}$/;

async function sha256Hex(text: string): Promise<string> {
  const digest = await crypto.subtle.digest('SHA-256', new TextEncoder().encode(text));
  return Array.from(new Uint8Array(digest), (b) => b.toString(16).padStart(2, '0')).join('');
}

// Tool-call ids carry the AI service's own prefix; the studio only ever sees neutral ones.
const toStudio = (id: string) => String(id).replace(/^toolu_/, 'call_');
const toService = (id: string) => String(id).replace(/^call_/, 'toolu_');

function cleanMessages(messages: any[]) {
  return messages.slice(-MAX_MESSAGES).map((m) => {
    if (!Array.isArray(m.content)) return { role: m.role, content: String(m.content ?? '') };

    return {
      role: m.role,
      content: m.content.map((block: any) =>
        block.type === 'tool_use' ? { ...block, id: toService(block.id) }
        : block.type === 'tool_result' ? { ...block, tool_use_id: toService(block.tool_use_id) }
        : block),
    };
  });
}

Deno.serve(async (req) => {
  try {
    const base44 = createClientFromRequest(req);
    const body = await req.json();
    const token = String(body.token || '');

    if (!HEX64.test(token) || !Array.isArray(body.messages) || body.messages.length === 0)
      return Response.json({ ok: false, error: 'invalid', message: 'That request was malformed.' }, { status: 400 });

    const computers = base44.asServiceRole.entities.StellaAccount;
    const wallets = base44.asServiceRole.entities.StellaWallet;
    const computer = (await computers.filter({ token_hash: await sha256Hex(token) }))[0];

    if (!computer)
      return Response.json({ ok: false, error: 'unknown', message: "Stella doesn't recognise this computer yet. Restart the studio while online." }, { status: 401 });

    const wallet = computer.linked_email ? (await wallets.filter({ user_email: computer.linked_email }))[0] : null;
    const balance = wallet ? (wallet.credits || 0) : (computer.credits || 0);

    if (balance <= 0)
      return Response.json({ ok: false, error: 'no_credits', message: "You're out of Stella AI credits.", credits: 0 }, { status: 402 });

    const mode = body.mode === 'build' ? 'build' : 'chat';
    const setup = MODES[mode];
    const guide = String(body.guide || '').slice(0, 120000);
    const context = String(body.context || '').slice(0, 60000);

    // The guide is the same on every request, so it's cached: later requests pay a tenth for it.
    const system: any[] = [{ type: 'text', text: PERSONA }];
    if (guide) system.push({ type: 'text', text: guide, cache_control: { type: 'ephemeral' } });
    if (context) system.push({ type: 'text', text: `This session:\n${context}` });

    const request: Record<string, unknown> = {
      model: setup.model,
      max_tokens: setup.maxTokens,
      system,
      messages: cleanMessages(body.messages),
    };

    if (Array.isArray(body.tools) && body.tools.length > 0) request.tools = body.tools;

    const answer = await fetch('https://api.anthropic.com/v1/messages', {
      method: 'POST',
      headers: {
        'x-api-key': Deno.env.get('ANTHROPIC_API_KEY') ?? '',
        'anthropic-version': '2023-06-01',
        'content-type': 'application/json',
      },
      body: JSON.stringify(request),
    });
    const data = await answer.json();

    if (!answer.ok) {
      console.error('AI service error', answer.status, JSON.stringify(data));   // in your logs only
      const busy = answer.status === 429 || answer.status === 529 || answer.status >= 500;
      return Response.json({
        ok: false,
        error: busy ? 'busy' : 'failed',
        message: busy ? 'Stella AI is busy right now. Please try again in a moment.' : "Stella AI couldn't answer that. Please try again.",
      }, { status: 502 });
    }

    const usage = data.usage || {};
    const weighted = ((usage.input_tokens || 0) + 1.25 * (usage.cache_creation_input_tokens || 0)
                    + 0.1 * (usage.cache_read_input_tokens || 0) + OUTPUT_WEIGHT * (usage.output_tokens || 0)) * setup.priceWeight;
    const used = Math.max(1, Math.ceil(weighted / TOKENS_PER_CREDIT));
    const credits = Math.max(0, balance - used);
    const inputTokens = usage.input_tokens || 0;
    const outputTokens = usage.output_tokens || 0;

    await computers.update(computer.id, {
      ...(wallet ? {} : { credits }),
      requests: (computer.requests || 0) + 1,
      input_tokens: (computer.input_tokens || 0) + inputTokens,
      output_tokens: (computer.output_tokens || 0) + outputTokens,
      last_seen: new Date().toISOString(),
    });

    if (wallet) {
      await wallets.update(wallet.id, {
        credits,
        requests: (wallet.requests || 0) + 1,
        input_tokens: (wallet.input_tokens || 0) + inputTokens,
        output_tokens: (wallet.output_tokens || 0) + outputTokens,
      });
    }

    // Only what the studio needs, in Fanan's own format.
    const content = (data.content || []).flatMap((block: any) =>
      block.type === 'text' ? [{ type: 'text', text: block.text }]
      : block.type === 'tool_use' ? [{ type: 'tool_use', id: toStudio(block.id), name: block.name, input: block.input }]
      : []);
    const reply = content.filter((b: any) => b.type === 'text').map((b: any) => b.text).join('\n').trim();

    const stop = data.stop_reason === 'tool_use' ? 'tools'
               : data.stop_reason === 'max_tokens' ? 'cut'
               : data.stop_reason === 'refusal' ? 'refused'
               : 'done';

    return Response.json({ ok: true, reply, content, stop, used, credits });
  } catch (error) {
    console.error(error);
    return Response.json({ ok: false, error: 'failed', message: "Stella AI couldn't answer that. Please try again." }, { status: 500 });
  }
});
```
