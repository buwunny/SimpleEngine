#include "core/PhysicsWorld.hpp"

PhysicsWorld::PhysicsWorld()
{
    collisionConfiguration = new btDefaultCollisionConfiguration();
    dispatcher = new btCollisionDispatcher(collisionConfiguration);
    overlappingPairCache = new btDbvtBroadphase();
    solver = new btSequentialImpulseConstraintSolver();
    dynamicsWorld = new btDiscreteDynamicsWorld(dispatcher, overlappingPairCache, solver, collisionConfiguration);
    dynamicsWorld->setGravity(btVector3(0, -9.81 * 2, 0));
}

PhysicsWorld::~PhysicsWorld()
{
    delete dynamicsWorld;
    delete solver;
    delete overlappingPairCache;
    delete dispatcher;
    delete collisionConfiguration;
}

void PhysicsWorld::addRigidBody(btRigidBody *body, short group, short mask)
{
    if (!dynamicsWorld || !body)
        return;
    dynamicsWorld->addRigidBody(body, group, mask);
}

void PhysicsWorld::removeRigidBody(btRigidBody *body)
{
    if (!dynamicsWorld || !body)
        return;
    dynamicsWorld->removeRigidBody(body);
}

void PhysicsWorld::rayTest(const btVector3 &from, const btVector3 &to, btCollisionWorld::RayResultCallback &result)
{
    if (!dynamicsWorld)
        return;
    dynamicsWorld->rayTest(from, to, result);
}

void PhysicsWorld::stepSimulation(float deltaTime, int maxSubSteps)
{
    if (!dynamicsWorld)
        return;
    dynamicsWorld->stepSimulation(deltaTime, maxSubSteps);
}

void PhysicsWorld::updateSingleAabb(btRigidBody *body)
{
    if (!dynamicsWorld || !body)
        return;
    dynamicsWorld->updateSingleAabb(body);
}

bool PhysicsWorld::blastOccluded(const btVector3 &from, const btVector3 &to) const
{
    if (!dynamicsWorld)
        return false;

    // The segment always runs between two body origins, both of which sit above
    // whatever floor they rest on, so a straight line between them can't dip
    // through the ground and self-occlude. Only a genuine wall in between hits.
    btCollisionWorld::AllHitsRayResultCallback cb(from, to);
    dynamicsWorld->rayTest(from, to, cb);
    for (int i = 0; i < cb.m_collisionObjects.size(); ++i)
    {
        const btCollisionObject *obj = cb.m_collisionObjects[i];
        if (obj && obj->isStaticObject())
            return true;
    }
    return false;
}

void PhysicsWorld::applyRadialBlast(const btVector3 &center, const BlastParams &params)
{
    if (!dynamicsWorld || params.radius <= 0.0f)
        return;

    const btVector3 up(0.0f, 1.0f, 0.0f);
    const btCollisionObjectArray &objects = dynamicsWorld->getCollisionObjectArray();
    for (int i = 0; i < objects.size(); ++i)
    {
        btRigidBody *body = btRigidBody::upcast(objects[i]);
        // Static/kinematic bodies have zero inverse mass; an impulse on them is
        // a no-op, so skip them before paying for the distance and ray checks.
        if (!body || body->getInvMass() <= 0.0f)
            continue;

        const btVector3 target = body->getWorldTransform().getOrigin();
        const btVector3 offset = target - center;
        const float dist = offset.length();
        if (dist >= params.radius)
            continue;

        // 1 - (d/r)^2, not plain linear. Linear falloff spends half the blast's
        // strength over the first half of the radius, which makes a rocket jump
        // demand near-perfect placement; this curve holds most of the power
        // across the near half and then falls away sharply toward the rim, so
        // there's a forgiving sweet spot but a distant blast is still a nudge.
        const float t = dist / params.radius;
        float falloff = 1.0f - t * t;

        // Cover weakens a blast rather than cancelling it. Cancelling reads as
        // the explosion having simply not happened, and the case that hits is
        // the one that matters most: a shot that detonates half-buried in the
        // surface it struck puts the blast centre on the far side of that
        // surface, so a hard cut would turn the most common rocket jump of all
        // -- straight down at the floor -- into an occasional silent dud.
        if (blastOccluded(center, target))
            falloff *= 0.35f;

        // Radial, then tilted toward +Y. Without the tilt a blast that goes off
        // beside you at floor level pushes you sideways *into* the floor, where
        // friction eats it; the tilt is what turns a near miss into a launch.
        btVector3 dir = dist > 1e-4f ? offset / dist : up;
        dir += up * params.upBias;
        if (dir.length2() < 1e-8f)
            dir = up;
        dir.normalize();

        // Multiply the velocity change back through by mass so what we hand
        // Bullet is an impulse. Applying a flat impulse instead would divide by
        // mass on the way in, so the number that gives the mass-10 player a
        // usable kick launches a mass-1 prop ten times as hard -- which is why
        // the old blast had to be tuned against the player's exact mass and
        // still sent props off at absurd speeds. Now distance alone decides.
        const float mass = 1.0f / body->getInvMass();
        body->activate(true);
        body->applyCentralImpulse(dir * (params.speed * falloff * mass));

        // A torque impulse, not a direct angular-velocity write: this one goes
        // through the body's angular factor, and the player capsule pins that to
        // zero. Writing the velocity would bypass it and roll the camera.
        if (params.spin > 0.0f)
        {
            btVector3 axis = dir.cross(up);
            if (axis.length2() < 1e-6f)
                axis = btVector3(1.0f, 0.0f, 0.0f);
            axis.normalize();
            body->applyTorqueImpulse(axis * (params.spin * falloff * mass));
        }
    }
}

bool PhysicsWorld::hasContacts(const btRigidBody *body) const
{
    if (!dynamicsWorld || !body)
        return false;

    // Contact points outlive the touch that created them: Bullet keeps a
    // manifold for any pair inside the broadphase margin and only prunes points
    // once they drift far enough apart. So the separation on each point has to
    // be checked too -- a point with positive distance is a near miss, and
    // treating it as a hit detonates a fired cow a frame or two early, visibly
    // short of whatever it was aimed at.
    const btScalar touchMargin = 0.01f;

    btDispatcher *disp = dynamicsWorld->getDispatcher();
    int numManifolds = disp->getNumManifolds();
    for (int i = 0; i < numManifolds; ++i)
    {
        const btPersistentManifold *manifold = disp->getManifoldByIndexInternal(i);
        if (manifold->getBody0() != body && manifold->getBody1() != body)
            continue;
        for (int p = 0; p < manifold->getNumContacts(); ++p)
        {
            if (manifold->getContactPoint(p).getDistance() <= touchMargin)
                return true;
        }
    }
    return false;
}
