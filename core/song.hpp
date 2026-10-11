#pragma once

#include "reply.hpp"
#include "shazam.hpp"

namespace shaza {

// Sizes the result screen is laid out for.
constexpr int kCoverPx = 200;
constexpr int kTitleW = 360, kTitleLines = 2;
constexpr int kArtistW = 320, kArtistLines = 1;

// A colour from the cover that reads on black: the most common colourful hue, lifted to a minimum
// brightness. Near-grey covers, or none, give a soft white.
Rgb accent_of(const Picture& cover);

// What the result screen needs from a match and its cover (which may be empty): the text laid out
// in Inter and the accent colour. A title the font cannot draw at all (a Chinese one, say) shows
// the artist in its place.
Reply make_reply(const shazam::Match& m, Picture cover);

}  // namespace shaza
