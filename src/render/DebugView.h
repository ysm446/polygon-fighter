#pragma once
namespace pf {
struct DebugView {
    bool bodies = true;
    bool models = true;
    bool physicalSkeleton = true;
    bool targetSkeleton = true;
    bool offsetTarget = true;
    bool hitboxes = true;
    bool hurtboxes = false;
    bool impacts = true;
};
}
