# Privilege

The GUI runs **unprivileged**; a small helper does the one privileged job per connection and exits. The console client runs as root, because for a terminal tool that is simpler and costs nothing.

## Why privilege is needed at all

Two things need `CAP_NET_ADMIN`: creating the tun interface, and running `vpnc-script` (which installs routes and rewrites DNS). Nothing else — authentication, TLS, the protocol, the UI — needs any privilege. Running a whole Qt application as root for those two steps is the wrong shape: Qt loads plugins, themes, fonts and image codecs, all as root; it breaks file ownership in your home; and Wayland compositors may refuse the connection outright.

## How the split works

libopenconnect supports the right architecture directly: `openconnect_setup_tun_fd()` accepts an already-open tun descriptor. So:

1. The GUI listens on a socket in `$XDG_RUNTIME_DIR` and launches `oneconnect-helper` through pkexec — a polkit prompt, not a terminal password.
2. The helper (root, libc-only) connects back, creates the tun device, and passes the fd over `SCM_RIGHTS`.
3. The GUI, unprivileged, hands the fd to `openconnect_setup_tun_fd()` and does the protocol, TLS, authentication and UI itself.
4. The helper runs `vpnc-script` on request (`connect`, MTU corrections, `disconnect`) and exits with the tunnel.

| component | privilege | links |
| --- | --- | --- |
| `oneconnect-gui` | your uid | Qt, openconnect, yaml-cpp, libsecret |
| `oneconnect-helper` | root, via pkexec | libc only |
| `oneconnect` (console) | root | openconnect, yaml-cpp, libsecret |

The helper connects *out* to a socket the GUI is listening on, so no root-owned endpoint sits on the system waiting to be found. Both ends check the other: the helper verifies the peer is the uid polkit authenticated (`PKEXEC_UID`) and that the socket is owned by that uid; the client verifies the peer is uid 0.

Everything the helper parses is attack surface, so the protocol is four verbs (`SETUP`, `SCRIPT`, `MTU`, `BYE`) and the environment it passes to `vpnc-script` is an **allowlist**: names must look like environment variables, values may not contain newlines, anything unrecognised is refused. The script is executed with `execl`, never a shell.

Because `openconnect_setup_tun_fd()` means openconnect does *not* run `vpnc-script` itself, `VpncEnvironment` rebuilds the environment openconnect would have constructed. Keep it in step with the helper's allowlist.

## What the split bought

Things a root GUI simply could not do:

- **The tray icon works.** `QSystemTrayIcon` needs `org.kde.StatusNotifierWatcher` on the session bus, which rejects uid 0.
- **The keychain works directly.** Same reason: the Secret Service lives on the session bus, and D-Bus authenticates its peer by uid via `SO_PEERCRED` — a bus owned by uid 1000 rejects uid 0 outright, whatever the socket permissions say. `SecretStore` keeps a fork-and-drop-privileges path only because the console client still runs as root.
- **Qt no longer runs as root.**

## The mistake to avoid

**Mozilla VPN (CVE-2023-4104)** shipped a privileged D-Bus service whose polkit check asked whether *the service* was authorised rather than the caller. The service ran as root, so the answer was always yes, and any local user could control the VPN. The equivalent mistake here is fetching `SO_PEERCRED` and never comparing it to anything — a check that is fetched but not evaluated is worse than no check, because it reads as a defence in review. The helper compares the peer uid to `PKEXEC_UID` and the socket owner to that same uid.

## Connecting without a password prompt

The shipped policy uses `auth_admin_keep`, the right default for anyone else installing this. `resources/linux/49-clavister-oneconnect.rules` is an opt-in polkit rule that removes the prompt for members of `network` in an active local session; it is not installed by `make install`, and the file documents exactly what it grants and how to undo it.

## What is deliberately not done

- **No setuid binary.** That would mean a program that parses YAML, speaks TLS and links Qt running with permanent elevated privilege.
- **No persistent daemon.** A root daemon idling between connections is a larger target than a helper spawned per connection and gone afterwards. (NetworkManager's persistent-daemon-plus-polkit design is the same architecture with a different lifetime tradeoff.)
