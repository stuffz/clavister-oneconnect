# Clavister OneConnect notes

Worked out against a live Clavister NetWall gateway in August 2026.

## OneConnect is OpenConnect underneath

Clavister's own knowledge base describes OneConnect as "OpenConnect based". The OneConnect interface in cOS Core speaks the AnyConnect protocol, so any OpenConnect-compatible client works; there is no Clavister-specific handshake. Clavister publishes no Linux client — their documented Linux path is plain `openconnect` on the command line.

Two details from their documentation: `anyconnect` (openconnect's default) is the correct protocol, and the name you connect to must match the CN or a SAN on the interface's certificate.

## The Host Name trap

The OneConnect interface has an optional field:

```
Host Name:  [ vpn.example.com ]
            (Optional) Limit server to only respond to matching hostname from client.
```

When set, the gateway compares the HTTP `Host:` header against that value and refuses anything else. Connect by IP and you get:

```
Connected to HTTPS on 203.0.113.10 with ciphersuite (TLS1.3)-...
Got inappropriate HTTP CONNECT response: HTTP/1.1 500 Internal Server Error
Creating SSL connection failed
```

Three things make this hard to diagnose:

1. **It fails after authentication succeeds.** TLS completes, the login is accepted, a session cookie is issued — only then is the tunnel CONNECT rejected. It looks like a tunnel problem, not an addressing one.
2. **The error is a bare 500.** No body, no mention of hostnames.
3. **A certificate warning appears alongside it** (`Server certificate verify failed: signer not found`), which sends you off investigating trust stores. It is a red herring — openconnect logs it and carries on.

The tell is the response time: the 500 comes back in ~50 ms, a server rejecting a request outright.

This is why `SessionOptions::gateway` is documented as an FQDN — the whole failure mode disappears if you never hand the library an address.

## Why KDE's NetworkManager applet cannot connect

The same bug seen from the other end, still unfixed upstream as of August 2026.

`nm-openconnect-service` invokes openconnect with a resolved **IP** as the gateway argument, which is guaranteed to fail against a gateway with Host Name set. NetworkManager-openconnect fixed this in [MR !20](https://gitlab.gnome.org/GNOME/NetworkManager-openconnect/-/merge_requests/20) by passing `--resolve $HOSTNAME:$IP $HOSTNAME:$PORT`, keeping a real hostname in the `Host:` header — but the fix never fires under KDE, because plasma-nm supplies its own auth dialog and stores the gateway using `openconnect_get_hostname()`:

```cpp
QString host(openconnect_get_hostname(d->vpninfo));
```

`get_hostname()` returns the library's *current* hostname, which openconnect rewrites when the server redirects during authentication — that is how it becomes an IP. plasma-nm then stores the IP, and the service has no hostname left for `--resolve`. The correct fix is one call: `openconnect_get_dnsname()`, the name as originally entered, immune to redirects. No KDE bug report exists for this.

## The authentication exchange is three forms

This gateway sends username, password and OTP as three **separate** forms:

```
auth form 1  name='username'  label='Username:'  type=1 (OC_FORM_OPT_TEXT)
auth form 2  name='password'  label='Password:'  type=2 (OC_FORM_OPT_PASSWORD)
auth form 3  name='otp'       label='OTP:'       type=1 (OC_FORM_OPT_TEXT)
```

Two things follow for password storage:

- "The password is in the first form" is false here; anything keying on form position silently misses it.
- The one-time code arrives as `OC_FORM_OPT_TEXT`, not `OC_FORM_OPT_PASSWORD` — so requiring password *type* excludes the OTP on this gateway, and the name-based exclusion in `IsStorablePassword()` covers gateways that send a code as a password field.

## Consequences for this client

- Always call `openconnect_set_hostname()` with the FQDN and never resolve it ourselves.
- Multi-node affinity, if ever added, must use `--resolve` semantics rather than substituting the address.
- The RADIUS challenge arrives as a second form after the first succeeds; caching form values across attempts would replay a spent one-time code, so OTP fields are never persisted.
