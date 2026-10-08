# Base44 prompt: Stella AI Cloud, version 4 (new credit prices)

Open the **Stella AI Cloud** app on Base44 and paste everything below the line into its AI chat. It only changes the prices of the credit packs; everything else stays as it is.

---

Update this app: change the prices of the three credit packs sold on the /buy page.

- The $5 pack now costs $20
- The $20 pack now costs $40
- The $50 pack now costs $80

Each pack keeps its name and the number of credits it gives now. Only the price changes (US dollars).

Make sure that:

1. The new prices are used everywhere the packs are defined or shown: the /buy page, any list or table of packs (in the code or in the database), and the backend function that creates the PayPal order.
2. The server decides the amount. It creates the PayPal order from its own pack list, never from a price sent by the page, and before adding credits it checks that the amount PayPal captured matches that pack's price.
3. Any per-credit price, "save X%" or "best value" label on the page is recalculated for the new prices, or removed if it no longer fits.
4. Nothing else changes: the 25 free credits, sign-in and linking, the credit balance, the PayPal capture and its duplicate check, and the stellaChat function all stay exactly as they are.

When you're done, tell me the three packs as they now stand: name, credits and price.
