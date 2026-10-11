# Base44 prompt: Stella AI Cloud, version 11 (two credit packs: $40 and $80)

Open the **Stella AI Cloud** app on Base44 and paste everything below the line into its AI chat. It only removes the $20 credit pack; everything else stays as it is.

---

Update this app: stop selling the $20 credit pack. Only two packs stay: the $40 pack and the $80 pack, each with its current name and number of credits.

Make sure that:

1. The $20 pack is gone everywhere the packs are defined or shown: the /buy page, any list or table of packs (in the code or in the database), and the backend function that creates the PayPal order.
2. The server refuses an order for the $20 pack (or any pack not in its list): it creates the PayPal order from its own pack list, never from a price sent by the page, and before adding credits it checks that the amount PayPal captured matches that pack's price.
3. Credits people already bought, and past purchases, stay exactly as they are.
4. Any per-credit price, "save X%" or "best value" label on the page is recalculated for the two packs, or removed if it no longer fits.

When you're done, tell me the packs as they now stand: name, credits and price.
