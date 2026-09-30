#pragma once

// Decides whether a failed login is the stored password's fault.
//
// Clavister checks the password before it sends the OTP form, and answers a
// wrong one with a 401 right there. So once a later form arrives without
// asking for the password again, the gateway has accepted it, and a failure
// after that is the one-time code. Forgetting the password then only makes
// the user type it again; keeping a rejected one retries it into a lockout.
enum class FormFields
{
    AsksForPassword,
    NoPassword
};

class StoredPasswordTracker
{
public:
    // Call before filling each form, so that a form never judges the password
    // it is itself submitting.
    void OnForm(FormFields fields)
    {
        if (state != State::Submitted)
        {
            return;
        }

        state = fields == FormFields::AsksForPassword ? State::Rejected : State::Accepted;
    }

    void OnStoredPasswordUsed()
    {
        state = State::Submitted;
    }

    bool Used() const
    {
        return state != State::Unused;
    }

    // Only meaningful after the login failed.
    bool ShouldForget() const
    {
        return state == State::Submitted || state == State::Rejected;
    }

private:
    enum class State
    {
        Unused,
        Submitted,
        Accepted,
        Rejected
    };

    State state = State::Unused;
};
