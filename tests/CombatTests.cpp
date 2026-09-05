#include "combat/CombatSystem.h"
#include "app/FixedClock.h"
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
}
int main() {
    try {
        pf::FighterPoses overlapping{};
        pf::CombatSystem combat;
        Require(combat.RequestPunch(0), "Idle must accept attack");
        Require(!combat.RequestPunch(0), "Attack must not restart");
        int activeFrames = 0;
        int hits = 0;
        for (int frame = 0; frame < 60; ++frame) {
            activeFrames += combat.HitboxActive(0) ? 1 : 0;
            const auto result = combat.Step(overlapping);
            hits += static_cast<int>(result.hitCount);
            if (frame < 21) Require(combat.Fighter(1).hp == 100, "Startup caused damage");
            if (frame == 21) {
                Require(result.hitCount == 1 && combat.Fighter(1).hp == 88, "First active frame must hit");
                Require(combat.Fighter(1).stunFrames == 24, "Hitstun must begin at full duration");
                Require(!combat.RequestPunch(1), "Hitstun must reject attack");
            }
        }
        Require(activeFrames == 8 && hits == 1, "One attack must hit once across exactly eight active frames");
        Require(combat.Fighter(0).State() == pf::CombatState::Idle, "Recovery must finish at frame 60");
        auto distant = overlapping;
        for (auto& part : distant[1]) part.position[0] = 100;
        combat.Reset();
        combat.RequestPunch(0);
        for (int i = 0; i < 60; ++i) combat.Step(distant);
        Require(combat.Fighter(1).hp == 100, "Out-of-range attack must miss");

        combat.Reset();
        combat.SetGuard(1, true);
        Require(!combat.RequestPunch(1), "Guard must reject attack");
        combat.RequestPunch(0);
        pf::CombatFrame blocked;
        for (int i = 0; i < 22; ++i) blocked = combat.Step(overlapping);
        Require(blocked.hitCount == 1 && blocked.hits[0].guarded, "Guard hit event missing");
        Require(combat.Fighter(1).hp == 97 && combat.Fighter(1).stunFrames == 8, "Guard must reduce damage and stun");
        for (int i = 0; i < 8; ++i) combat.Step(overlapping);
        Require(combat.Fighter(1).State() == pf::CombatState::Guarding, "Held guard must resume after stun");
        combat.SetGuard(1, false);
        Require(combat.RequestPunch(1), "Released guard must permit attack");

        combat.Reset();
        combat.RequestPunch(0);
        combat.RequestPunch(1);
        pf::CombatFrame trade;
        for (int i = 0; i < 22; ++i) trade = combat.Step(overlapping);
        Require(trade.hitCount == 2, "Simultaneous hits must trade");
        for (std::size_t i = 0; i < 2; ++i) {
            Require(combat.Fighter(i).hp == 88 && combat.Fighter(i).attackFrame == -1, "Trade must damage and interrupt both fighters");
            Require(!combat.HitboxActive(i), "Interrupted attack must lose hitbox");
        }
        combat.Reset();
        for (int attack = 0; attack < 9; ++attack) {
            Require(combat.RequestPunch(0), "Next attack unavailable after recovery");
            for (int i = 0; i < 60; ++i) combat.Step(overlapping);
        }
        Require(combat.Fighter(1).hp == 0 && combat.Fighter(1).State() == pf::CombatState::KO, "HP must clamp at zero and enter KO");
        combat.SetGuard(1, true);
        Require(!combat.RequestPunch(1) && combat.Fighter(1).State() == pf::CombatState::KO, "KO must reject combat actions");
        combat.Reset();
        Require(combat.Fighter(1).hp == 100 && combat.Fighter(1).State() == pf::CombatState::Idle, "Reset must restore match");
        for (int rate : {30,60,144}) {
            combat.Reset();
            combat.RequestPunch(0);
            pf::FixedClock clock;
            int count = 0;
            for (int frame = 0; frame < rate; ++frame)
                clock.Advance(1.0 / rate, false, false, [&] { combat.Step(overlapping); ++count; });
            Require(count == 60 && combat.Fighter(1).hp == 88 && combat.Fighter(0).attackFrame == -1,
                "Render cadence must not change attack frames or damage");
        }
        std::cout << "PASS: startup/active/recovery, single hit, miss, guard, stun, trade, interrupt, KO, reset, 30/60/144 Hz\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
