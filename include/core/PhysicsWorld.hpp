#ifndef PHYSICSWORLD_HPP
#define PHYSICSWORLD_HPP

#include <btBulletDynamicsCommon.h>

class PhysicsWorld
{
public:
    PhysicsWorld();
    ~PhysicsWorld();

    btDiscreteDynamicsWorld *getWorld() { return dynamicsWorld; }

    // Convenience wrappers
    void addRigidBody(btRigidBody *body, short group = btBroadphaseProxy::DefaultFilter, short mask = btBroadphaseProxy::AllFilter);
    void removeRigidBody(btRigidBody *body);
    void rayTest(const btVector3 &from, const btVector3 &to, btCollisionWorld::RayResultCallback &result);
    void setGravity(const btVector3 &g)
    {
        if (dynamicsWorld)
            dynamicsWorld->setGravity(g);
    }
    btVector3 getGravity() const { return dynamicsWorld ? dynamicsWorld->getGravity() : btVector3(0, 0, 0); }
    void stepSimulation(float deltaTime, int maxSubSteps = 10);
    void updateSingleAabb(btRigidBody *body);

    // One explosion's shape. Bundled into a struct because the same set travels
    // from the .cow script through Scene, out over the wire, and into the VFX --
    // an argument list this long gets miscopied at every hop.
    struct BlastParams
    {
        float radius = 6.0f;  // metres; nothing past this is touched at all
        float speed = 24.0f;  // peak velocity change (m/s), at the centre
        float upBias = 0.35f; // how far the push tilts toward +Y (0 = pure radial)
        float spin = 8.0f;    // peak angular kick, for tumbling debris
    };

    // Shove every dynamic rigidbody near `center` away from it. The push is a
    // velocity change rather than a raw impulse, so one set of numbers is
    // correct for the heavy player and for light props at the same time; see
    // the implementation for the falloff, the up-tilt and the occlusion rule.
    void applyRadialBlast(const btVector3 &center, const BlastParams &params);

    // True if `body` is *actually touching* something right now. Note this is
    // stricter than "has a contact manifold": Bullet keeps a manifold alive, and
    // its cached points with it, for a pair that has merely come close, so
    // testing manifold existence alone detonates a projectile just before impact.
    bool hasContacts(const btRigidBody *body) const;

private:
    // True if static world geometry blocks the straight line from `from` to
    // `to`, which weakens (never cancels — see applyRadialBlast) the push that
    // reaches the far end. Only static bodies count: debris and other players
    // are not allowed to shadow a blast, or a cow drifting through the gap
    // would make the knockback flicker on and off between frames.
    bool blastOccluded(const btVector3 &from, const btVector3 &to) const;

    btDefaultCollisionConfiguration *collisionConfiguration;
    btCollisionDispatcher *dispatcher;
    btBroadphaseInterface *overlappingPairCache;
    btSequentialImpulseConstraintSolver *solver;
    btDiscreteDynamicsWorld *dynamicsWorld;
};

#endif // PHYSICSWORLD_HPP
