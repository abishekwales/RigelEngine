/* Copyright (C) 2016, Nikolai Wuttke. All rights reserved.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "physics_system.hpp"

#include "engine/collision_checker.hpp"
#include "engine/entity_tools.hpp"

namespace ex = entityx;


namespace rigel { namespace engine {

using namespace std;

using components::BoundingBox;
using components::Physical;
using components::SolidBody;
using components::WorldPosition;


// TODO: This is implemented here, but declared in physical_components.hpp.
// It would be cleaner to have a matching .cpp file for that file.
BoundingBox toWorldSpace(
  const BoundingBox& bbox,
  const base::Vector& entityPosition
) {
  return bbox + base::Vector(
    entityPosition.x,
    entityPosition.y - (bbox.size.height - 1));
}


PhysicsSystem::PhysicsSystem(const engine::CollisionChecker* pCollisionChecker)
  : mpCollisionChecker(pCollisionChecker)
{
}


void PhysicsSystem::update(ex::EntityManager& es) {
  using components::CollidedWithWorld;

  es.each<Physical, WorldPosition, BoundingBox, components::Active>(
    [this](
      ex::Entity entity,
      Physical& physical,
      WorldPosition& position,
      const BoundingBox& collisionRect,
      const components::Active&
    ) {
      const auto originalPosition = position;

      const auto movementX = static_cast<int16_t>(physical.mVelocity.x);
      if (movementX != 0) {
        position= applyHorizontalMovement(
          toWorldSpace(collisionRect, position),
          position,
          movementX,
          physical.mCanStepUpStairs);
      }

      // Cache new world space BBox after applying horizontal movement
      // for the next steps
      const auto bbox = toWorldSpace(collisionRect, position);

      // Apply Gravity after horizontal movement, but before vertical
      // movement. This is so that if the horizontal movement results in the
      // entity floating in the air, we want to drop down already in the same
      // frame where we applied the horizontal movement. Changing the velocity
      // here will automatically move the entity down when doing the vertical
      // movement.
      if (physical.mGravityAffected) {
        physical.mVelocity.y = applyGravity(bbox, physical.mVelocity.y);
      }

      const auto movementY = static_cast<std::int16_t>(physical.mVelocity.y);
      if (movementY != 0) {
        std::tie(position, physical.mVelocity.y) = applyVerticalMovement(
          bbox,
          position,
          physical.mVelocity.y,
          movementY,
          physical.mGravityAffected);
      }

      const auto collisionOccured =
        position != originalPosition + WorldPosition{movementX, movementY};
      setTag<CollidedWithWorld>(entity, collisionOccured);
    });
}


base::Vector PhysicsSystem::applyHorizontalMovement(
  const BoundingBox& bbox,
  const base::Vector& currentPosition,
  const int16_t movementX,
  const bool allowStairStepping
) const {
  const auto movingRight = movementX > 0;
  base::Vector newPosition = currentPosition;

  auto movingBbox = bbox;
  for (auto step = 0; step < std::abs(movementX); ++step) {
    const auto move = movingRight ? 1 : -1;

    const auto isTouching = movingRight
      ? mpCollisionChecker->isTouchingRightWall(movingBbox)
      : mpCollisionChecker->isTouchingLeftWall(movingBbox);
    if (isTouching) {
      if (allowStairStepping) {
        // TODO: This stair-stepping logic is only needed for the player.
        // It should be implemented as part of a dedicated player physics
        // system/module which is separate from the generic physics system.
        auto stepUpBbox = movingBbox;
        stepUpBbox.topLeft.y -= 1;
        const auto collisionAfterStairStep = movingRight
          ? mpCollisionChecker->isTouchingRightWall(stepUpBbox)
          : mpCollisionChecker->isTouchingLeftWall(stepUpBbox);

        if (!collisionAfterStairStep) {
          stepUpBbox.topLeft.x += move;
          if (mpCollisionChecker->isOnSolidGround(stepUpBbox)) {
            movingBbox.topLeft.x += move;
            movingBbox.topLeft.y -= 1;
            newPosition.x += move;
            newPosition.y -= 1;
            continue;
          }
        }
      }

      break;
    }

    movingBbox.topLeft.x += move;
    newPosition.x += move;
  }

  return newPosition;
}


float PhysicsSystem::applyGravity(
  const BoundingBox& bbox,
  const float currentVelocity
) {
  if (currentVelocity == 0.0f) {
    if (mpCollisionChecker->isOnSolidGround(bbox)) {
      return currentVelocity;
    }

    // We are floating - begin falling
    return 1.0f;
  } else {
    // Apply gravity to falling object until terminal velocity reached
    if (currentVelocity < 2.0f) {
      return currentVelocity + 0.56f;
    } else {
      return 2.0f;
    }
  }
}


std::tuple<base::Vector, float> PhysicsSystem::applyVerticalMovement(
  const BoundingBox& bbox,
  const base::Vector& currentPosition,
  const float currentVelocity,
  const int16_t movementY,
  const bool beginFallingOnHittingCeiling
) const {
  base::Vector newPosition = currentPosition;
  const auto movingDown = movementY > 0;

  auto movingBbox = bbox;
  for (auto step = 0; step < std::abs(movementY); ++step) {
    const auto isTouching = movingDown
      ? mpCollisionChecker->isOnSolidGround(movingBbox)
      : mpCollisionChecker->isTouchingCeiling(movingBbox);
    if (isTouching) {
      if (movingDown || !beginFallingOnHittingCeiling) {
        // For falling, we reset the Y velocity as soon as we hit the ground
        return make_tuple(newPosition, 0.0f);
      } else {
        // For jumping, we begin falling early when we hit the ceiling
        return make_tuple(newPosition, 1.0f);
      }
    }

    const auto move = movingDown ? 1 : -1;
    movingBbox.topLeft.y += move;
    newPosition.y += move;
  }

  return make_tuple(newPosition, currentVelocity);
}

}}
