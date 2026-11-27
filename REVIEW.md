# Review rules for this repo

Read by the Claude Review workflow on every pull request. Breaking one of these
is a compliance violation, or a security one where marked.

- **This repo is public.** No tokens, passwords, Wi-Fi details, ntfy topic
  names, order numbers, purchase dates, home addresses or phone numbers, in
  code, comments, docs or commit messages. (Security.)
- **The board updates itself from the `firmware` branch over Wi-Fi.** A change
  to the update path (`firmware/sourdough_box/github.ino`, `firmware/tools/
  release.py`) must not be able to install a build on the wrong board, skip the
  SHA-256 check, or leave a board unable to fetch the next update. A board that
  can't update has to be fixed by hand with a USB cable. (Bugs.)
- **One codebase, one build per board.** A new board type goes in both
  `firmware/sourdough_box/board.h` and `TARGETS` in `firmware/tools/release.py`;
  parts on the STEMMA QT chain are detected at startup, not built in.
- **Firmware runs on an ESP32-S3 with a 60 s watchdog.** Nothing may block the
  main loop for long; network calls need timeouts. (Performance, Bugs.)
- **`FW_VERSION` in `version.h` goes up with every firmware change**, or boards
  never install it.
- **`live.html` is the public dashboard.** It must not contain the pause key or
  push topic, even as a placeholder.
- Docs (`README.md`) change with the behaviour they describe, in the same PR.
