# VaultRDP Auth/Reconnect Integration Checks

## Preconditions
- Build and run with `--debug`:
  - `./vaultrdp --debug`
- Ensure at least one RDP connection exists.

## 1) No saved password prompts before connect
1. Edit a connection and disable saved credential, or save username without password.
2. Connect the session.
3. Verify a credentials prompt appears before connection attempt.
4. Verify username is prefilled when available and editable.

## 2) Wrong credentials prompts and cancel closes tab
1. Enter incorrect credentials.
2. Verify authentication fails and credentials prompt appears.
3. Press `Cancel`.
4. Verify session tab closes immediately.

## 3) Wrong credentials retry then success
1. Enter incorrect credentials first.
2. On prompt retry, enter correct credentials.
3. Verify connection succeeds.
4. Disconnect unexpectedly (network drop or remote reset) and verify auto-reconnect behavior remains normal.

## 4) Auto-reconnect blocked after auth failure
1. Connect successfully once.
2. Force auth failure (bad password).
3. Verify app does not keep auto-reconnecting in background.
4. Verify reconnect resumes only after explicit user action (`Connect` or in-tab reconnect).

## 5) Auth prompt rate-limit and cap
1. Repeatedly submit bad credentials.
2. Verify prompt timing backs off between attempts.
3. Verify prompt attempts cap out (tab closes after cap).

## 6) Last successful username persistence
1. Connect successfully with username `userA`.
2. Close tab.
3. Trigger credential prompt again for same connection (no saved password).
4. Verify username defaults to `userA`.
