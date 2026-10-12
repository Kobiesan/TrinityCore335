#ifndef TRINITY_AQUATIC_CHASE_H
#define TRINITY_AQUATIC_CHASE_H
class Unit;
class Creature;
struct Position;
// Preserve existing aquatic combat during a short player jump over water.
bool GetAquaticJumpChasePosition(Unit const* target, Creature const* pursuer, Position& waterPosition);
#endif
