// Port-specific QSTR definitions for D26 Badge MicroPython integration
// These are additional Q-strings needed by our badge module.
// The build system's qstr scanner should find most of them automatically,
// but we list extras here for safety.

// Badge module names
Q(badge)
Q(display)
Q(leds)
Q(buttons)

// Display methods
Q(clear)
Q(text)
Q(rect)
Q(pixel)
Q(show)

// LED methods
Q(set)
Q(fill)

// Button methods
Q(get)
Q(is_pressed)
Q(wait)

// Control
Q(exit)
Q(delay_ms)
