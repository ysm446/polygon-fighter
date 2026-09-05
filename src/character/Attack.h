#pragma once

namespace pf {
enum class AttackKind { Punch, Kick };
inline const char* AttackName(AttackKind kind) { return kind == AttackKind::Kick ? "Kick" : "Punch"; }
}
