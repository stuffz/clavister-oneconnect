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

## DTLS dies at the rekey, and the gateway does not notice

Seen three times against a NetWall 510 in September 2026. The gateway asks for an in-place DTLS rehandshake with `X-DTLS-Rekey-Method: ssl`, then answers the rehandshake it asked for with a TLS fatal alert:

```
DTLS rekey due
DTLS handshake failed: A TLS fatal alert has been received.
DTLS handshake failed: Resource temporarily unavailable, try again.   (every 72 s, forever)
```

openconnect does the only correct thing with a fatal alert, closes DTLS and sends over TLS. The gateway keeps its half of the association: the OneConnect status page still reads `CONNECTED (UDP/DTLS)`, and return traffic is pushed into a UDP flow the client has closed. Outbound is accepted, nothing comes back, and CSTP DPD is answered on the TLS socket the whole time, so neither end declares the tunnel dead. openconnect retries the dead `X-DTLS-Session-ID` every attempt period and never escalates; only a new CONNECT hands out a session ID the gateway will answer.

The 72 s is openconnect's arithmetic: a 12 s handshake deadline plus our 60 s `dtlsAttemptPeriod`.

The interval belongs to the gateway and has already changed once, so nothing should key on it. The client does not print the CONNECT headers, so these come from when the rekeys fired: the first two incidents rekeyed DTLS 28800 s (8h) after it came up, with the CSTP rekey 200 s ahead of it; the session that connected 2026-09-19 rekeyed at 115200 s (32h), CSTP at 115000 s, the same 200 s lead.

`DtlsWatchdog` is the workaround. When a DTLS cipher that was reported goes away for longer than a handshake could take, the client pauses the mainloop and re-enters it, which makes openconnect reconnect with the cookie it still holds. A second drop inside 30 minutes disables DTLS for the session instead of flapping the tunnel. `dtls: false` in the profile sidesteps the whole thing at the cost of TCP-over-TCP; the CSTP rekey at the same mark works.

Verified on 2026-09-20, the first rekey the watchdog was live for:

```
09:26:31 [INFO ] DTLS rekey due
09:26:31 [ERROR] DTLS handshake failed: A TLS fatal alert has been received.
09:26:47 [INFO ] DTLS lost; reconnecting the session to restore it
09:26:47 [INFO ] Caller paused the connection
09:26:47 [INFO ] Got CONNECT response: HTTP/1.1 200 CONNECTED
09:26:47 [INFO ] Established DTLS connection (using GnuTLS). Ciphersuite (DTLS1.2)-(ECDHE-RSA)-(AES-128-GCM).
```

16 s from the alert to the reconnect: the 15 s grace plus a poll tick. The tunnel came back on the same `tun0` with no re-auth, held one tun fd, and was still carrying traffic over DTLS four hours later.

## Consequences for this client

- Always call `openconnect_set_hostname()` with the FQDN and never resolve it ourselves.
- Multi-node affinity, if ever added, must use `--resolve` semantics rather than substituting the address.
- The RADIUS challenge arrives as a second form after the first succeeds; caching form values across attempts would replay a spent one-time code, so OTP fields are never persisted.
- DPD is not liveness against this gateway. A tunnel can answer DPD indefinitely while forwarding nothing; the DTLS state is the signal that something changed.
