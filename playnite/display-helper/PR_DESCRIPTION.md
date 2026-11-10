This adds a few Display Helper features for multi-monitor setups:

- **Real monitor names** instead of the adapter name plus `\\.\DISPLAYn`, numbered the way Windows Settings numbers them. Settings also gets an **Identify** button.
- **Custom monitor names** in settings.
- **New option to move the game window instead of changing the primary display.** When a game targets another display, its window is moved there as a borderless window, so the taskbar and desktop icons stay put. This is a global setting with a per-game override.
- **"Show current settings"** game menu item, plus ✓ marks on the choices that are active for the game.

This is based on the **1.30 release**, not on current `master`, which is mid-rewrite. A direct merge will conflict. If you want any of these features, I can port them to the new code.

Tested on Windows 11 with Playnite 10.64 and three monitors.

🤖 Generated with [Claude Code](https://claude.com/claude-code)
