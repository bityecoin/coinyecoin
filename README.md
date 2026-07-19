# Fishsticks wallet update #2 — paper wallet

Unzip into your source root (`coinyecoin-2.3.0-fishsticks/`), overwriting, then
rebuild. `preview.png` shows exactly what the dialog now renders (not part of
the tree — reference only).

## What changed
- `src/qt/res/icons/paper_wallet.png` — replaced with your new blank template.
- `src/qt/forms/paperwalletdialog.ui` — QR codes repositioned into the LOAD &
  VERIFY / SPEND boxes; the address and private-key labels changed from vertical
  to horizontal and placed in the PUBLIC ADDRESS / PRIVATE KEY boxes. Coordinates
  were measured from the 1672x941 template and verified against your sample.
- `src/qt/utilitydialog.cpp` — the renderer now breaks each (space-less) hash
  into fixed-width lines and auto-shrinks a monospace font to fit its box
  (QLabel can't wrap a hash with no spaces), replacing the old vertical-text
  fitting. Uses `QFontMetrics::width()` so it stays Qt 5.7.1-safe.

Validated with uic + moc; UI XML is well-formed.

## Importing a paper wallet
There's no separate import dialog; use the private key (the WIF on the SPEND
side). In the wallet: Help > Debug window > Console:

    importprivkey "5J...WIF..." "paper" true

The last arg rescans so the balance appears. For an encrypted wallet, run
`walletpassphrase "yourpass" 120` first. Importing several? Pass `false` to skip
per-key rescans, then run `rescanblockchain` once at the end.
