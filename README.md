# Fishsticks wallet update #5 — icon colors round 2 (on top of update-4)

Unzip into your source root, overwrite, rebuild. Four files. `tx-icons-preview.png`
shows the new transaction icon colors.

Addresses everything from the last screenshots:

- **About Coinyecoin Core dialog** (`utilitydialog.cpp`): the license text was
  light-on-light and invisible. The about area now has a dark background with
  light text, so it's readable and matches the theme.
- **Print paper wallet** menu icon (`bitcoingui.cpp`): was a raw dark icon while
  the other menu items were light — now colored light to match.
- **About Coinyecoin Core** menu icon (`bitcoingui.cpp`): was flattened to a solid
  white circle — now shows the actual Coinye coin logo (uses the icon's own color).
- **Transaction icons** (`transactiontablemodel.cpp`): the blanket "keep original"
  from update-4 left the monochrome icons invisible. Now colored per meaning:
    - Status column: green = confirmed/confirming, gold = pending/immature,
      red = conflicted/abandoned (the "?" / clocks / checks are all visible now).
    - Type column: received/sent keep their colorful fish sticks; the mined
      icon is gold; send-to-self / other are light.
- **Overview "Recent transactions"** (`overviewpage.cpp`): was force-colored to a
  flat light silhouette (all-white blobs). Now uses the model's colored icon, so
  it matches the Transactions tab (colorful fish sticks, gold mined, etc.).

## Sanity sweep (you asked)
I checked every remaining icon-colorizing call. All the others are button glyphs
(add / remove / copy / paste / send / edit / address-book in the send, receive,
address-book, sign/verify, coin-control and debug dialogs) and the status-bar
lock / HD icons. Those are simple shapes on button/bar backgrounds and read fine
as light — no change needed. Nothing else was broken.

On "red if not working" for the bottom sync tick: that indicator only has
synced (green tick) and catching-up (green spinner) states in this build; a true
red error state would need a small addition to the sync logic — say the word.

Validated: moc + brace balance. Couldn't cross-compile here, so if anything
errors on your build, paste it and we'll fix.
