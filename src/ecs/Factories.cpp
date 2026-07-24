#include "ecs/Factories.hpp"

#include "core/PhysicsWorld.hpp"
#include "meshes/Mesh.hpp"
#include "meshes/CubeMesh.hpp"
#include "meshes/PlaneMesh.hpp"
#include "meshes/StaticMesh.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace ecs
{
    namespace
    {
        int g_nextEntityId = 1;

        Identity makeIdentity(const std::string &prefix)
        {
            Identity id;
            id.id = g_nextEntityId++;
            id.name = prefix + " " + std::to_string(id.id);
            return id;
        }
    }

    void initializeTransformFromModel(Transform &t, const glm::mat4 &m)
    {
        glm::vec3 sx;
        sx.x = glm::length(glm::vec3(m[0][0], m[0][1], m[0][2]));
        sx.y = glm::length(glm::vec3(m[1][0], m[1][1], m[1][2]));
        sx.z = glm::length(glm::vec3(m[2][0], m[2][1], m[2][2]));
        t.localScale = sx;
        t.model = m;

        t.modelNoScale = m;
        if (sx.x != 0.0f)
        {
            t.modelNoScale[0][0] /= sx.x;
            t.modelNoScale[0][1] /= sx.x;
            t.modelNoScale[0][2] /= sx.x;
        }
        if (sx.y != 0.0f)
        {
            t.modelNoScale[1][0] /= sx.y;
            t.modelNoScale[1][1] /= sx.y;
            t.modelNoScale[1][2] /= sx.y;
        }
        if (sx.z != 0.0f)
        {
            t.modelNoScale[2][0] /= sx.z;
            t.modelNoScale[2][1] /= sx.z;
            t.modelNoScale[2][2] /= sx.z;
        }

        t.position = glm::dvec3(m[3][0], m[3][1], m[3][2]);
        t.scale = glm::dvec3(t.localScale);

        glm::vec3 col0 = (sx.x > 0.f) ? glm::vec3(m[0][0], m[0][1], m[0][2]) / sx.x : glm::vec3(1, 0, 0);
        glm::vec3 col1 = (sx.y > 0.f) ? glm::vec3(m[1][0], m[1][1], m[1][2]) / sx.y : glm::vec3(0, 1, 0);
        glm::vec3 col2 = (sx.z > 0.f) ? glm::vec3(m[2][0], m[2][1], m[2][2]) / sx.z : glm::vec3(0, 0, 1);
        t.rotation.y = glm::degrees(glm::atan(-col0[2], glm::sqrt(col1[2] * col1[2] + col2[2] * col2[2])));
        t.rotation.x = glm::degrees(glm::atan(col1[2], col2[2]));
        t.rotation.z = glm::degrees(glm::atan(col0[1], col0[0]));
    }

    void applyTransform(Registry &r, Entity e, const glm::vec3 &pos,
                        const glm::vec3 &rotDeg, const glm::vec3 &scale)
    {
        auto *t = r.try_get<Transform>(e);
        if (!t)
            return;

        glm::mat4 m = glm::translate(glm::mat4(1.0f), pos);
        m = glm::rotate(m, glm::radians(rotDeg.z), glm::vec3(0.0f, 0.0f, 1.0f));
        m = glm::rotate(m, glm::radians(rotDeg.y), glm::vec3(0.0f, 1.0f, 0.0f));
        m = glm::rotate(m, glm::radians(rotDeg.x), glm::vec3(1.0f, 0.0f, 0.0f));
        m = glm::scale(m, scale);

        initializeTransformFromModel(*t, m);
        // The matrix decomposition above can disagree with the caller's exact
        // Euler triple at gimbal-lock — favor what we were given.
        t->position = glm::dvec3(pos);
        t->rotation = glm::dvec3(rotDeg);
        t->scale = glm::dvec3(scale);

        auto *p = r.try_get<Physics>(e);
        if (p && p->body)
        {
            if (p->shape)
                p->shape->setLocalScaling(btVector3(t->localScale.x, t->localScale.y, t->localScale.z));
            btTransform trans;
            trans.setFromOpenGLMatrix(glm::value_ptr(t->modelNoScale));
            p->body->setWorldTransform(trans);
            if (p->motion)
                p->motion->setWorldTransform(trans);
            p->body->activate(true);
        }
    }

    void setMass(Physics &p, double mass)
    {
        p.mass = mass;
        if (p.body && p.shape)
        {
            btVector3 inertia(0, 0, 0);
            p.shape->calculateLocalInertia(mass, inertia);
            p.body->setMassProps(mass, inertia);
        }
    }

    namespace
    {
        // Reduce a mesh's vertices to the corner points of its convex hull:
        // deduplicate on a 1e-5 quantization grid (raw OBJ data repeats a vertex
        // per face), then let Bullet's solver discard everything interior.
        std::vector<btVector3> computeHullPoints(const float *verts, size_t vertexCount,
                                                 int floatsPerVertex)
        {
            btConvexHullShape hull;
            std::set<std::array<int32_t, 3>> seen;
            for (size_t i = 0; i < vertexCount; ++i)
            {
                const float *v = verts + i * floatsPerVertex;
                std::array<int32_t, 3> key{
                    static_cast<int32_t>(std::lround(v[0] * 100000.0f)),
                    static_cast<int32_t>(std::lround(v[1] * 100000.0f)),
                    static_cast<int32_t>(std::lround(v[2] * 100000.0f))};
                if (!seen.insert(key).second)
                    continue;
                hull.addPoint(btVector3(v[0], v[1], v[2]), false);
            }
            hull.setMargin(0.005f);
            hull.optimizeConvexHull();

            std::vector<btVector3> points;
            points.reserve(static_cast<size_t>(hull.getNumPoints()));
            const btVector3 *p = hull.getUnscaledPoints();
            for (int i = 0; i < hull.getNumPoints(); ++i)
                points.push_back(p[i]);
            return points;
        }

        // A convex hull is a property of the mesh, not of the instance, but the
        // reduction above is expensive — cow.obj is 4583 vertices and took ~1.8 ms,
        // so every shot fired dropped a frame. Derive the points once per mesh and
        // rebuild only the cheap shape around them per instance; the shape has to
        // stay per-instance because Bullet stores the entity's scale on it (see
        // setLocalScaling in buildPhysics).
        std::unique_ptr<btCollisionShape> makeHullShape(const std::shared_ptr<Mesh> &mesh,
                                                        const float *verts, size_t vertexCount,
                                                        int floatsPerVertex)
        {
            // Meshes come from AssetManager, which never evicts, and the pinning
            // shared_ptr below keeps an address from being recycled under the key.
            struct Entry
            {
                std::shared_ptr<Mesh> pin;
                std::vector<btVector3> points;
            };
            static std::unordered_map<const Mesh *, Entry> cache;

            const std::vector<btVector3> *points = nullptr;
            std::vector<btVector3> local; // used when the mesh isn't shared/cacheable
            if (mesh)
            {
                auto it = cache.find(mesh.get());
                if (it == cache.end())
                    it = cache.emplace(mesh.get(),
                                       Entry{mesh, computeHullPoints(verts, vertexCount, floatsPerVertex)})
                             .first;
                points = &it->second.points;
            }
            else
            {
                local = computeHullPoints(verts, vertexCount, floatsPerVertex);
                points = &local;
            }

            if (points->empty())
                return std::make_unique<btBoxShape>(btVector3(1.0f, 1.0f, 1.0f));
            auto hull = std::make_unique<btConvexHullShape>(
                &(*points)[0].getX(), static_cast<int>(points->size()), sizeof(btVector3));
            hull->setMargin(0.005f);
            hull->recalcLocalAabb();
            return hull;
        }

        // Build a Physics component around a freshly-constructed shape using
        // the entity's transform. Caller still owns the shape's destruction
        // via the unique_ptr we move in.
        void buildPhysics(Registry &r, Entity e, std::unique_ptr<btCollisionShape> shape, double mass)
        {
            auto *t = r.try_get<Transform>(e);
            if (!t)
                return;

            // Apply the entity's local scale to the shape before computing
            // inertia — otherwise Bullet's tensor is wrong for non-unit scale.
            shape->setLocalScaling(btVector3(t->localScale.x, t->localScale.y, t->localScale.z));

            btVector3 localInertia(0, 0, 0);
            if (mass != 0.0)
                shape->calculateLocalInertia(mass, localInertia);

            btTransform transform;
            transform.setFromOpenGLMatrix(glm::value_ptr(t->modelNoScale));
            auto motion = std::make_unique<btDefaultMotionState>(transform);

            btRigidBody::btRigidBodyConstructionInfo rbInfo(mass, motion.get(), shape.get(), localInertia);
            auto body = std::make_unique<btRigidBody>(rbInfo);
            body->setActivationState(ACTIVE_TAG);
            body->setUserPointer(toUserPointer(e));

            // Continuous collision detection: without this, a small/fast dynamic
            // body (a rocket-speed fired cow, say) can cross more than its own
            // size in a single tick and tunnel straight through thin geometry
            // like the ground plane's 0.02-unit-thick collision box, since
            // discrete collision detection only checks overlap at tick
            // boundaries. Only worth enabling for bodies that actually move
            // (mass 0 = static).
            if (mass != 0.0)
            {
                btVector3 center;
                btScalar radius = 0.0f;
                shape->getBoundingSphere(center, radius);
                if (radius > 0.0f)
                {
                    body->setCcdMotionThreshold(radius);
                    body->setCcdSweptSphereRadius(radius * 0.5f);
                }
            }

            Physics phys;
            phys.shape = std::move(shape);
            phys.motion = std::move(motion);
            phys.body = std::move(body);
            phys.mass = mass;
            r.emplace<Physics>(e, std::move(phys));
        }
    }

    Entity createCube(Registry &r, PhysicsWorld *physics, int size,
                      const glm::mat4 &model, const glm::vec4 &color, float mass)
    {
        Entity e = r.create();
        Identity ident = makeIdentity("Cube");
        r.emplace<Identity>(e, std::move(ident));

        Transform t;
        initializeTransformFromModel(t, model);
        r.emplace<Transform>(e, std::move(t));

        Renderable rd;
        rd.mesh = std::make_shared<CubeMesh>(size);
        rd.color = color;
        r.emplace<Renderable>(e, std::move(rd));

        ShapeMarker sm;
        sm.kind = ShapeKind::Cube;
        sm.cubeSize = size;
        r.emplace<ShapeMarker>(e, sm);

        auto shape = std::make_unique<btBoxShape>(btVector3(size / 2.0f, size / 2.0f, size / 2.0f));
        buildPhysics(r, e, std::move(shape), mass);

        if (physics)
            physics->addRigidBody(r.get<Physics>(e).body.get());
        return e;
    }

    Entity createPlane(Registry &r, PhysicsWorld *physics, float length, float width,
                       const glm::mat4 &model, const glm::vec4 &color, float mass)
    {
        Entity e = r.create();
        r.emplace<Identity>(e, makeIdentity("Plane"));

        Transform t;
        initializeTransformFromModel(t, model);
        r.emplace<Transform>(e, std::move(t));

        Renderable rd;
        rd.mesh = std::make_shared<PlaneMesh>(length, width, length / 5.0f, width / 5.0f);
        rd.color = color;
        r.emplace<Renderable>(e, std::move(rd));

        ShapeMarker sm;
        sm.kind = ShapeKind::Plane;
        sm.planeLength = length;
        sm.planeWidth = width;
        r.emplace<ShapeMarker>(e, sm);

        auto shape = std::make_unique<btBoxShape>(btVector3(length / 2.0f, 0.01f, width / 2.0f));
        buildPhysics(r, e, std::move(shape), mass);
        if (auto *p = r.try_get<Physics>(e); p && p->body)
            p->body->setFriction(1.0f);

        if (physics)
            physics->addRigidBody(r.get<Physics>(e).body.get());
        return e;
    }

    Entity createStaticObject(Registry &r, PhysicsWorld *physics,
                              std::shared_ptr<Mesh> sharedMesh,
                              const float *verts, size_t vertexCount,
                              const unsigned int *indices, size_t indexCount,
                              int floatsPerVertex,
                              const glm::mat4 &model, const glm::vec4 &color, float mass)
    {
        (void)indices;
        (void)indexCount;
        Entity e = r.create();
        r.emplace<Identity>(e, makeIdentity("StaticObject"));

        Transform t;
        initializeTransformFromModel(t, model);
        r.emplace<Transform>(e, std::move(t));

        Renderable rd;
        rd.mesh = sharedMesh ? sharedMesh
                             : std::make_shared<StaticMesh>(verts, vertexCount, indices, indexCount, floatsPerVertex);
        rd.color = color;
        r.emplace<Renderable>(e, std::move(rd));

        ShapeMarker sm;
        sm.kind = ShapeKind::Static;
        r.emplace<ShapeMarker>(e, sm);

        std::unique_ptr<btCollisionShape> shape;
        if (vertexCount == 0 || floatsPerVertex < 3)
            shape = std::make_unique<btBoxShape>(btVector3(1.0f, 1.0f, 1.0f));
        else
            shape = makeHullShape(sharedMesh, verts, vertexCount, floatsPerVertex);
        buildPhysics(r, e, std::move(shape), mass);

        if (physics)
            physics->addRigidBody(r.get<Physics>(e).body.get());
        return e;
    }

    Entity createPlayer(Registry &r, PhysicsWorld *physics, Camera *camera, const glm::mat4 &model)
    {
        Entity e = r.create();
        Identity ident;
        ident.id = g_nextEntityId++;
        ident.name = "Player";
        r.emplace<Identity>(e, std::move(ident));

        Transform t;
        initializeTransformFromModel(t, model);
        r.emplace<Transform>(e, std::move(t));

        ShapeMarker sm;
        sm.kind = ShapeKind::Player;
        r.emplace<ShapeMarker>(e, sm);

        r.emplace<PlayerTag>(e);

        PlayerController pc;
        pc.camera = camera;
        pc.lastX = 1920 / 2.f;
        pc.lastY = 1080 / 2.f;
        r.emplace<PlayerController>(e, std::move(pc));

        const float playerMass = 10.0f;
        auto shape = std::make_unique<btCapsuleShape>(0.5f, 1.0f);
        btVector3 localInertia(0, 0, 0);
        shape->calculateLocalInertia(playerMass, localInertia);

        btTransform transform;
        transform.setFromOpenGLMatrix(glm::value_ptr(model));
        auto motion = std::make_unique<btDefaultMotionState>(transform);

        btRigidBody::btRigidBodyConstructionInfo rbInfo(playerMass, motion.get(), shape.get(), localInertia);
        auto body = std::make_unique<btRigidBody>(rbInfo);
        body->setAngularFactor(btVector3(0, 0, 0));
        body->setCcdMotionThreshold(0.5);
        body->setCcdSweptSphereRadius(0.4);
        body->setFriction(1.0f);
        body->setUserPointer(toUserPointer(e));

        Physics phys;
        phys.shape = std::move(shape);
        phys.motion = std::move(motion);
        phys.body = std::move(body);
        phys.mass = playerMass;
        r.emplace<Physics>(e, std::move(phys));

        if (physics)
            physics->addRigidBody(r.get<Physics>(e).body.get());
        return e;
    }
}
