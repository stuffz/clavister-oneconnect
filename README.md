# clavister-oneconnect

> **Unofficial.** This project is not affiliated with, endorsed by, or supported by Clavister AB. "Clavister" and "OneConnect" are trademarks of their respective owner and are used here only to describe which gateways this client can connect to.

A Linux VPN client for Clavister OneConnect and other AnyConnect-compatible gateways, built on `libopenconnect`. Clavister ships no Linux client; this one speaks the same protocol and shows you what the tunnel actually did — it reconciles the routes the gateway pushed against what the kernel installed, and reports split vs full tunnel, DNS, MTU, and whether DTLS came up.

Two binaries share one connection store: `oneconnect` (console, runs as root) and `oneconnect-gui` (Qt tray application, runs unprivileged — tun setup is delegated to a small pkexec-elevated helper).

## Install

- Arch: `make package` (runs `makepkg -si` on a copy of `packaging/arch/PKGBUILD`, so the tracked file is not rewritten)
- Ubuntu 24.04: `packaging/debian/build-deb.sh` builds a `.deb` into `dist/` (needs docker)

## Building

C++17, `libopenconnect`, `yaml-cpp`, `libsecret`, and Qt 6 Widgets for the GUI. Qt is optional: without it, `make` builds the console client and says so.

| Distro | Packages |
| ------ | -------- |
| Arch / CachyOS | `openconnect vpnc yaml-cpp qt6-base libsecret base-devel` |
| Debian / Ubuntu | `libopenconnect-dev vpnc-scripts libyaml-cpp-dev qt6-base-dev libsecret-1-dev build-essential` |
| Fedora | `openconnect-devel vpnc-script yaml-cpp-devel qt6-qtbase-devel libsecret-devel gcc-c++ make` |

```bash
make                # both binaries -> build/release/
make cli            # console client only, no Qt needed
make install        # into $PREFIX (default /usr/local)
make test           # QTest suite under tests/, via CMake and CTest
make container-test # the same inside the build image; nothing installs on the host
```

`vpnc-script` installs routes and DNS once the tunnel is up. Distros place it differently (`/etc/vpnc/` on Arch and Fedora, `/usr/share/vpnc-scripts/` on Debian and Ubuntu); the client searches both at connect time.

## Usage

```bash
oneconnect-gui                          # GUI, no sudo
sudo oneconnect --profile Work          # console, saved connection
sudo oneconnect --gateway vpn.example.com
oneconnect --list                       # what is saved; --help for the rest
```

Connections live in `~/.config/clavister-oneconnect/connections.yaml`, shared by both binaries — [connections.yaml.example](connections.yaml.example) documents every field. Passwords go to the desktop keychain when *Remember password* is on; one-time codes are never stored.

In the route report, `missing` means the gateway pushed a route that `vpnc-script` failed to install, and `unexpected` is a route on the tunnel interface that was never pushed (often legitimate). The `/32` host route pinning the gateway to your physical interface is outside the tunnel interface and not listed.

Logging is always on, at `~/.local/state/clavister-oneconnect/oneconnect.log`. `--debug` mirrors everything to the console; `--trace` additionally dumps raw HTTP. Credentials are redacted from every log line, but redaction is a best-effort denylist — read a trace log before sharing it.

**Connect by hostname, never by IP.** Gateways with Clavister's *Host Name* restriction reject IP-addressed tunnel requests with a bare HTTP 500 *after* login succeeds — see [docs/CLAVISTER.md](docs/CLAVISTER.md).

## Documentation

- [docs/CLAVISTER.md](docs/CLAVISTER.md) — gateway quirks: the Host Name trap, the plasma-nm bug, the three-form auth exchange, the DTLS rekey that strands the tunnel
- [docs/PRIVILEGE.md](docs/PRIVILEGE.md) — the unprivileged GUI and the helper split

## Licence

MIT. `libopenconnect` is LGPL-2.1 and dynamically linked. Icon attribution in [ATTRIBUTION.md](ATTRIBUTION.md).
