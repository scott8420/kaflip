#pragma once
#include <string_view>

// ─────────────────────────────────────────────────────────────────────────────
// core/Proof -- the proofing checks (s012). GTK-free, no deps: plain UTF-8 in,
// answers out, so Optik can run the same list after recognition.
//
// s012 m1 starts it with the scene break; the canned checks (doubled quotes,
// name look-alikes, broken paragraphs ...) join it in later milestones.
// paginate.js carries a copy of is_scene_break for the page -- keep the two
// in step (the selftest pins this one).
// ─────────────────────────────────────────────────────────────────────────────
namespace kaflip::core {

// A paragraph whose whole text is a scene-break mark: "* * *", "***", "⁂",
// "#", "~ ~ ~", "• • •", "---", "___". At least one ornament (* # ~ ⁂ ∗ ✱ ❧ ❦
// • · § +), or three or more rule characters (- _ = – —); nothing else but
// spaces (and dots between ornaments). At most 24 characters. "...", a lone
// "—", empty text, and anything with a letter or digit are NOT breaks.
bool is_scene_break(std::string_view utf8);

}  // namespace kaflip::core
