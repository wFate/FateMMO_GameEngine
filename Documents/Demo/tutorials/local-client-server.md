# Tutorial: Local Client-Server Setup

Audience: Full Game / Server

This tutorial is for local full-tree development when `FateEngine` and `FateServer` are available. It is not guaranteed to work in the public demo-only package.

The public demo primarily demonstrates the engine/editor workflow. Server-authoritative MMO play requires the proprietary server layer, database configuration, auth/session setup, and compatible client build.

## Goal

Run a local server and connect one or more clients for testing.

## Requirements

- Full local checkout with `game/` and `server/`.
- PostgreSQL database configured for the server.
- Required migrations applied.
- Compatible `FateEngine.exe` and `FateServer.exe` built from the same protocol version.
- Server identity key available for secure Noise_NK validation, if the client requires it.

## Build Server

Use the wrapper:

```powershell
.\scripts\check_shipping.ps1 -Preset x64-Release -Target FateServer
```

If the executable is still running, the build may fail with `LNK1168`. Stop the server process before rebuilding.

## Build Client

Use:

```powershell
.\scripts\check_shipping.ps1 -Preset x64-Release -Target FateEngine
```

The client and server must agree on `PROTOCOL_VERSION`.

## Start Server

The local launcher defaults to Release:

```powershell
.\run_server.ps1
```

Watch the server log for:

- Database connection success.
- Startup cache load success.
- UDP bind success.
- Server identity key status.
- Protocol version.

If a duplicate server is already running, the bind should fail instead of silently sharing the port.

## Start Client

Launch `FateEngine.exe` from its output folder or through your usual local dev shortcut.

Connect to the local server address and sign in with a test account.

## Multi-Client Testing

For multiple clients:

1. Start the server once.
2. Launch two client instances.
3. Sign in with two different accounts.
4. Load characters in the same scene.
5. Confirm entity enter/update/leave replication works.
6. Move both characters.
7. Test zone transition.
8. Disconnect one client and confirm the other sees the leave.

## Troubleshooting

Version mismatch:

- Rebuild both client and server.
- Confirm `engine/net/packet.h` has the same `PROTOCOL_VERSION` in both binaries.

Auth proof rejected:

- Check login/session setup.
- Check server identity key configuration.
- Confirm the client sends `CmdAuthProof` after the Noise_NK handshake.

Server bind failure:

- Another `FateServer.exe` may already be running.
- Stop the old process and restart once.

Database startup cache problems:

- Verify migrations applied.
- Check that the server is pointed at the correct database.
- Restart after applying migrations.

Network congestion or missing entities:

- Check per-opcode network telemetry if enabled.
- Verify critical-lane packet behavior.
- Reproduce with a small scene first.

