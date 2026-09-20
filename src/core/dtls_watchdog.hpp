#pragma once

#include <cstdint>

// Decides what to do when DTLS disappears from a tunnel that had it.
//
// Some gateways (Clavister NetWall, at the X-DTLS-Rekey-Time they advertise)
// refuse the DTLS rehandshake they themselves asked for, then keep their side
// of the association open and route return traffic into it. openconnect falls
// back to TLS for sending, retries the dead session ID every attempt period,
// and never escalates. The only recovery is a new CONNECT, which this asks for
// by having the caller pause and re-enter the mainloop.
//
// Pure logic with the clock passed in, so it can be tested without openconnect.
enum class DtlsAction
{
    None,
    Reconnect, // pause the mainloop and re-enter it: new CONNECT, new DTLS session
    Disable    // DTLS dropped again too soon; stop trying and stay on TLS
};

class DtlsWatchdog
{
public:
    // openconnect_get_dtls_cipher() reports nothing during the CONNECTING state
    // a healthy rekey passes through, and openconnect gives a handshake 12 s
    // before declaring it failed. Waiting longer than that means an empty
    // cipher is the retry loop, not a rekey in flight.
    static constexpr std::int64_t LostGraceSeconds = 15;

    // A long session legitimately needs one reconnect per rekey, and the
    // gateway sets that interval in hours. Two drops inside this window is
    // flaky UDP, and flapping the tunnel for it costs more than TLS-only does.
    static constexpr std::int64_t ReconnectWindowSeconds = std::int64_t{30} * 60;

    // Feed every observation; `established` is whether a DTLS cipher is
    // currently reported. Returns what the caller should do right now.
    DtlsAction Observe(bool established, std::int64_t now)
    {
        if (disabled)
        {
            return DtlsAction::None;
        }

        if (established)
        {
            seenUp = true;
            lostSince = NotLost;
            actedOnThisLoss = false;
            return DtlsAction::None;
        }

        // Never had DTLS: blocked UDP or a TLS-only profile, nothing to recover.
        if (!seenUp || actedOnThisLoss)
        {
            return DtlsAction::None;
        }

        if (lostSince == NotLost)
        {
            lostSince = now;
            return DtlsAction::None;
        }

        if (now - lostSince < LostGraceSeconds)
        {
            return DtlsAction::None;
        }

        actedOnThisLoss = true;

        if (lastReconnect != NotLost && now - lastReconnect < ReconnectWindowSeconds)
        {
            disabled = true;
            return DtlsAction::Disable;
        }

        lastReconnect = now;
        return DtlsAction::Reconnect;
    }

    bool Disabled() const
    {
        return disabled;
    }

private:
    static constexpr std::int64_t NotLost = -1;

    bool seenUp = false;
    bool actedOnThisLoss = false;
    bool disabled = false;
    std::int64_t lostSince = NotLost;
    std::int64_t lastReconnect = NotLost;
};
