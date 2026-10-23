# Security

This is a game remaster you build from source and run locally. It reads your own copy of the game and writes only
under its own folders (`work/`, `%LOCALAPPDATA%\AndRemaster\`). It sends nothing over the network; the only download
is the optional texture pack, when you give `tools/setup_assets.py` a URL.

To report a vulnerability (for example a crafted save, session or asset file that executes code), use GitHub's
private reporting: **Security > Report a vulnerability** on this repository. Please do not open a public issue for it.
Expect an answer within two weeks. Supported version: the latest commit on `main`.
