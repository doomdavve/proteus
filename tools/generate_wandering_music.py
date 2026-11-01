#!/usr/bin/env python3
"""A more flowing piano melody over the unchanged Cozy samples and accompaniment."""
from generate_cozy_music import CHANNELS, pattern, write_module

# Sixteen two-bar phrases: an outward stroll followed by a higher answering arc.
# C-major steps and small turns connect chord tones; the final D resolves to E
# at the loop's beginning. No octave jumps, accents, fills or layer changes.
PHRASES = (
    (53, 56, 58, 61, 60, 58, 56, 53),  # C6: up to C, turn home
    (51, 53, 56, 58, 60, 58, 56, 53),  # G6: climb, then descend
    (54, 56, 58, 61, 65, 63, 61, 58),  # Fmaj7: open into the upper register
    (56, 53, 51, 49, 51, 53, 56, 58),  # C6: down to low C, return
    (61, 60, 58, 56, 53, 56, 58, 61),  # Am7: falling and rising arch
    (58, 56, 54, 53, 54, 58, 61, 63),  # F6: a small dip, longer climb
    (61, 58, 54, 56, 58, 61, 63, 61),  # Dm7: weave around chord tones
    (60, 58, 56, 53, 51, 53, 56, 58),  # G: leave the answer open
    (61, 63, 65, 63, 61, 58, 56, 53),  # C6: answer from above
    (56, 58, 60, 63, 65, 63, 60, 58),  # G6: rise a little further
    (61, 63, 65, 68, 65, 63, 61, 58),  # Fmaj7: gentle high point, no accent
    (56, 58, 61, 60, 58, 56, 53, 51),  # C6: gradual descent
    (53, 56, 58, 61, 63, 61, 58, 56),  # Am7: one more ascending question
    (54, 53, 51, 49, 53, 54, 58, 61),  # F6: lower turn and response
    (63, 61, 58, 56, 54, 56, 58, 54),  # Dm7: settle back down
    (56, 58, 60, 58, 56, 53, 51, 51),  # G: a quiet lead-in to the loop
)
# Two gentle eighth-note turns per phrase, with space for the woodwind at row 28.
ROWS = (0, 4, 6, 10, 14, 18, 22, 26)


def wandering_pattern(index):
    phrases = [tuple(zip(ROWS, PHRASES[index * 2 + half])) for half in range(2)]
    return pattern(index, piano_phrases=phrases)


def main():
    write_module("a-little-boat-wander.xm", "Little boat - wander", wandering_pattern,
                 channels=CHANNELS + 2)


if __name__ == "__main__":
    main()
