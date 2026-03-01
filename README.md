# VaultRDP

VaultRDP is a desktop RDP client focused on managing connections, gateways, and credentials in an encrypted local vault.

## Current Features

- Embedded FreeRDP session rendering in-app (no external RDP window).
- Works in Linux desktop environments (Wayland/X11 via Qt).
- Folder tree with:
  - Folders
  - Connections
  - Gateways
  - Drag-and-drop move support for folders/connections/gateways.
- Connection management:
  - Add/edit/duplicate/delete/rename.
  - Per-connection options:
    - Clipboard enabled/disabled.
    - Map home drive enabled/disabled.
  - Auto reconnect behavior.
- Gateway management:
  - Add/edit/duplicate/delete/rename.
  - Credential modes:
    - Use connection credentials
    - Saved credentials
    - Prompt each time
  - Scope option:
    - `Gateway can be used from any folder`.
- Credential prompts:
  - Prompts when credentials are missing.
  - Re-prompts on authentication failure.
  - Gateway auth failures are handled separately from session auth failures.
- Vault security:
  - First-start encryption setup wizard with opt-out.
  - Lock/unlock behavior tied to encryption state.
  - Vault Settings dialog with encryption status and vault location.
  - Enable/disable encryption and change password.
  - Passphrase policy (uppercase/lowercase/number).
- Logging:
  - Logs written to the VaultRDP state directory.
  - Rotation on launch: `vaultrdp.log` + last 5 logs.
  - `--debug` flag enables verbose debug logging and a status bar indicator.

## Runtime Data Location

VaultRDP stores runtime files under:

- `~/.local/share/VaultRDP/`

Including:

- `vaultrdp.db`
- `vaultrdp.log`
- `vaultrdp.log.1` ... `vaultrdp.log.5`

## Ubuntu Dependencies (apt)

For Ubuntu 24.04+, install:

```bash
sudo apt update
sudo apt install -y \
  build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-base-dev-tools qt6-tools-dev-tools \
  libsqlite3-dev libssl-dev libsodium-dev libargon2-dev \
  libfreerdp3-dev libfreerdp-client3-dev libwinpr3-dev
```

Notes:

- VaultRDP uses `pkg-config` for FreeRDP/WinPR and crypto libraries.
- If you build FreeRDP from source in a custom prefix (for example `/opt/freerdp-3.23`), ensure `PKG_CONFIG_PATH` and `LD_LIBRARY_PATH` include that prefix.

## Build

From the project root:

```bash
cmake -S . -B build -G Ninja
cmake --build build -j
```

## Run

```bash
./build/vaultrdp
```

With verbose logging:

```bash
./build/vaultrdp --debug
```

## Tests

```bash
ctest --test-dir build --output-on-failure
```

