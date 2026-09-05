#include "debug/DebugUI.h"
#include "physics/PhysicsWorld.h"
#include <imgui.h>

namespace pf {
void DrawDebugUI(PhysicsWorld& physics, DebugState& state, const char* adapter, bool debugLayer) {
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(350, 690), ImGuiCond_FirstUseEver);
    ImGui::Begin("Polygon Fighter | Physics Lab", nullptr, ImGuiWindowFlags_NoCollapse);
    ImGui::PushItemWidth(170);
    ImGui::Text(physics.IsCombatScene() ? "PHASE 07 / KICK" : "PHYSICS / ANIMATION LAB");
    ImGui::Separator();
    ImGui::TextWrapped("GPU: %s", adapter);
    ImGui::Text("DX12 validation: %s", debugLayer ? "enabled" : "unavailable / release");
    ImGui::Text("Render: %.1f FPS", ImGui::GetIO().Framerate);
    ImGui::Text("Physics: 60 Hz | %llu steps", static_cast<unsigned long long>(state.steps));
    ImGui::Spacing();
    ImGui::Checkbox("Pause [Space]", &state.paused);
    ImGui::SameLine();
    ImGui::BeginDisabled(!state.paused);
    state.singleStep = ImGui::Button("Step [N]") || state.singleStep;
    ImGui::EndDisabled();
    state.reset = ImGui::Button("Reset scene [R]") || state.reset;
    if (!physics.IsCombatScene()) {
        ImGui::SameLine();
        if (ImGui::Button("Impulse [I]")) physics.PushBox();
    }
    if (ImGui::SliderFloat("Gravity", &state.gravity, -20.0f, 0.0f, "%.2f m/s2"))
        physics.SetGravity(state.gravity);
    ImGui::Separator();
    if (!physics.IsCombatScene()) {
        const auto p = physics.BoxPose().position;
        ImGui::Text("Box: %.2f, %.2f, %.2f m", p[0], p[1], p[2]);
        ImGui::Text("Speed: %.2f m/s", physics.BoxSpeed());
        ImGui::Text("Body: %s", physics.BoxActive() ? "active" : "sleeping");
        ImGui::TextDisabled("Ground 20 x 20 m | Box 1 m / 10 kg");
    }
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Humanoid / 11 bodies / 10 joints");
    ImGui::TextDisabled(physics.IsCombatScene() ? "Pelvis supported / movable" : "Pelvis anchored to world");
    bool changed = ImGui::Checkbox("Joint motors", &state.joints.enabled);
    changed = ImGui::SliderFloat("Stiffness", &state.joints.stiffness, 0, 1500, "%.0f Nm/rad") || changed;
    changed = ImGui::SliderFloat("Damping", &state.joints.damping, 0, 100, "%.1f Nms/rad") || changed;
    changed = ImGui::SliderFloat("Max torque", &state.joints.maxTorque, 0, 250, "%.0f Nm") || changed;
    if (ImGui::Button("Default motors")) { state.joints = JointTuning{}; changed = true; }
    if (changed) physics.SetJointTuning(state.joints);
    if (ImGui::Button("Push torso [H]")) physics.PushHumanoid();
    ImGui::SameLine();
    if (ImGui::Button(physics.IsCombatScene() ? "Reset match" : "Reset pose")) physics.ResetHumanoid();
    ImGui::Text("Max joint error: %.2f deg", physics.HumanoidPoseError());
    ImGui::TextWrapped("Push the torso and compare recovery with different motor settings.");
    ImGui::Separator();
    ImGui::Text("Animation: %s | %.2f s", physics.Attacking() ? AttackName(physics.CurrentAttack()) : "Idle", physics.AnimationTime());
    ImGui::BeginDisabled(physics.IsCombatScene() ? physics.Combat().Fighter(0).State() != CombatState::Idle : physics.Attacking());
    if (ImGui::Button("Punch [P]")) physics.RequestPunch();
    ImGui::EndDisabled();
    if (physics.IsCombatScene()) ImGui::TextDisabled("Combat playback: fixed 1x");
    else if (ImGui::SliderFloat("Playback speed", &state.playbackSpeed, .25f, 2, "%.2fx"))
        physics.SetPlaybackSpeed(state.playbackSpeed);
    ImGui::Checkbox("Physical bodies", &state.view.bodies);
    ImGui::Checkbox("Blender models (off: boxes)", &state.view.models);
    ImGui::Checkbox("Physical skeleton (cyan)", &state.view.physicalSkeleton);
    ImGui::Checkbox("Target skeleton (green)", &state.view.targetSkeleton);
    ImGui::Checkbox("Target offset +1.8 m", &state.view.offsetTarget);
    ImGui::Checkbox("Hitboxes (orange)", &state.view.hitboxes);
    ImGui::Checkbox("Hurtboxes (blue)", &state.view.hurtboxes);
    ImGui::Checkbox("Impact point / impulse", &state.view.impacts);
    ImGui::PopItemWidth();
    ImGui::End();
    if (physics.IsCombatScene()) {
        ImGui::SetNextWindowPos(ImVec2(390,20), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(620,250), ImGuiCond_FirstUseEver);
        ImGui::Begin("Combat | P1 white / P2 red", nullptr, ImGuiWindowFlags_NoCollapse);
        if (ImGui::BeginTable("Fighters", 2)) {
            for (std::size_t i = 0; i < 2; ++i) {
                ImGui::TableNextColumn();
                ImGui::PushID(static_cast<int>(i));
                const auto& fighter = physics.Combat().Fighter(i);
                ImGui::Text("P%u  HP %d / 100", static_cast<unsigned>(i + 1), fighter.hp);
                ImGui::ProgressBar(fighter.hp / 100.0f, ImVec2(-1,16), "");
                ImGui::Text("%s | stun %d", StateName(fighter.State()), fighter.stunFrames);
                if (fighter.attackFrame >= 0) {
                    const auto& attack = CombatSystem::Data(fighter.attack);
                    const char* phase = fighter.attackFrame < attack.startupFrames ? "Startup"
                        : fighter.attackFrame < attack.startupFrames + attack.activeFrames ? "Active" : "Recovery";
                    ImGui::Text("%s %s | %d / %d", AttackName(fighter.attack), phase, fighter.attackFrame + 1,
                        attack.startupFrames+attack.activeFrames+attack.recoveryFrames);
                } else ImGui::TextDisabled("No attack");
                ImGui::BeginDisabled(fighter.State() != CombatState::Idle);
                if (ImGui::Button(i == 0 ? "Punch [P]" : "Punch [K]")) physics.RequestPunch(i);
                ImGui::SameLine();
                if (ImGui::Button(i == 0 ? "Kick [F]" : "Kick [L]")) physics.RequestAttack(i,AttackKind::Kick);
                ImGui::EndDisabled();
                ImGui::Checkbox(i == 0 ? "Auto guard [hold G]" : "Auto guard [hold O]", &state.guard[i]);
                const auto& reaction = physics.HitReactions()[i];
                if (reaction.visibleFrames > 0)
                    ImGui::Text("Hit: %s%s", HumanoidParts[reaction.hit.bone].name, reaction.hit.guarded ? " (guard)" : "");
                else ImGui::TextDisabled("No recent hit");
                ImGui::PopID();
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Punch: 21 / 8 / 31 frames | Damage 12 (guard 3) | Stun 24 (guard 8)");
        ImGui::TextDisabled("Kick: 30 / 10 / 38 frames | Damage 18 (guard 4) | Stun 30 (guard 12)");
        ImGui::Text("Distance %.2f m | A/D, Left/Right: back/forward",physics.FighterDistance());
        ImGui::End();
        ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 260, ImGui::GetIO().DisplaySize.y - 225));
        ImGui::SetNextWindowSize(ImVec2(240,205), ImGuiCond_FirstUseEver);
        ImGui::Begin("Hit reaction", nullptr, ImGuiWindowFlags_NoCollapse);
        ImGui::PushItemWidth(-1);
        ImGui::Text("Hit impulse");
        bool hitChanged = ImGui::SliderFloat("##Impulse", &state.hitReaction.impulse, 0, 40, "%.1f Ns");
        ImGui::Text("Guard impulse scale");
        hitChanged = ImGui::SliderFloat("##GuardScale", &state.hitReaction.guardMultiplier, 0, 1, "%.2fx") || hitChanged;
        if (ImGui::Button("Default hit reaction")) { state.hitReaction = {}; hitChanged = true; }
        if (hitChanged) physics.SetHitReactionTuning(state.hitReaction);
        ImGui::PopItemWidth();
        ImGui::TextDisabled("Yellow: hit | Cyan: guard");
        ImGui::TextDisabled("Line: 0.025 m per Ns");
        ImGui::End();
    }
}
}
