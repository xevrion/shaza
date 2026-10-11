#pragma once

namespace dev {

// HTTPS certificates have validity dates, and the board has no clock chip, so it starts in 1970
// and every certificate looks "not yet valid". Sets the clock from NTP, or, when campus firewalls
// block NTP, from the Date header of a plain HTTP response. Returns true once the time is
// plausible. Same as in cassette-orb.
bool sync_time();

}  // namespace dev
