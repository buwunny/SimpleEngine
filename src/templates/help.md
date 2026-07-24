# Cow Engine - Help

## Getting Started

Cow Engine is a C++ game engine editor. Use the **Scene** tab to build your scene, the **Code** tab to write scripts, and this **Help** tab as a reference.

To attach a script to an object, select it in the Scene Hierarchy, enter a `.cow` file path in the Inspector's Script field, then click **Edit** to open it in the Code tab. Hit **Apply (recompile)** to compile and run it.

---

# CowScript Reference

CowScript is Cow Engine's built-in scripting language. Scripts are plain text files with the `.cow` extension. Each object in the scene can have one script attached to it.

---

## Language Basics

### Comments

```
// This is a comment
```

Lines beginning with `//` are ignored by the interpreter.

### Variables

```
let x = 10
let name = "player"
let active = true
let nothing = null
```

Declare variables with `let`. Variables are dynamically typed — they can hold numbers, strings, booleans, or null.

### Data Types

| Type   | Examples                  |
|--------|---------------------------|
| Number | `0`, `3.14`, `-1`, `1e3`  |
| Bool   | `true`, `false`           |
| Str    | `"hello"`, `""`           |
| Null   | `null`                    |
| Handle | returned by engine calls  |

### Operators

**Arithmetic:** `+`, `-`, `*`, `/`, `%`

**Comparison:** `==`, `!=`, `<`, `<=`, `>`, `>=`

**Logical:** `and`, `or`, `not`

**Assignment:** `=`

**Property access:** `.` (dot notation on handles)

---

## Control Flow

### If / Else

```
if (x > 0) {
    print("positive")
} else {
    print("non-positive")
}
```

The `else` branch is optional.

### While Loop

```
let i = 0
while (i < 10) {
    print(i)
    i = i + 1
}
```

Loops have a built-in safety limit of 1,000,000 iterations to prevent infinite loops from freezing the engine.

---

## Functions

Define reusable functions with `fn`:

```
fn add(a, b) {
    return a + b
}

let result = add(3, 4)
print(result)  // prints 7
```

Functions can return values with `return`. Calling a function without enough arguments is safe — missing parameters default to `null`.

---

## Events

Events are special functions the engine calls automatically. Define them with `on`:

```
on start() {
    // runs once when testing mode begins
}

on update(dt) {
    // runs every frame while testing
    // dt = delta time in seconds since last frame
}
```

Only `start` and `update` are currently supported. `update` receives `dt` (delta time) as its first argument — use it to make movement frame-rate independent.

---

## Built-in Functions

### Utility

| Function | Description |
|----------|-------------|
| `print(...)` | Logs values to the Debug Console. Accepts any number of arguments. |
| `time()` | Returns elapsed time in seconds since testing began. |
| `dt()` | Returns last frame's delta time in seconds. |
| `key(name)` | Returns `true` if the named key is currently held. |

**Key names for `key()`:**

Letters `a`–`z`, and: `space`, `enter`, `shift`, `ctrl`, `alt`, `up`, `down`, `left`, `right`, `escape`

```
if (key("space")) {
    self_apply_impulse(0, 5, 0)
}
```

### Math

| Function | Description |
|----------|-------------|
| `sin(x)` | Sine of x (radians) |
| `cos(x)` | Cosine of x (radians) |
| `tan(x)` | Tangent of x (radians) |
| `sqrt(x)` | Square root |
| `abs(x)` | Absolute value |
| `floor(x)` | Round down to nearest integer |
| `ceil(x)` | Round up to nearest integer |
| `random()` | Random float in [0, 1) |

---

## Self — The Attached Object

These functions read and write the transform of the object the script is attached to.

### Reading Position / Rotation / Scale

| Function | Returns |
|----------|---------|
| `self_x()` | World X position |
| `self_y()` | World Y position |
| `self_z()` | World Z position |
| `self_rx()` | X rotation (degrees) |
| `self_ry()` | Y rotation (degrees) |
| `self_rz()` | Z rotation (degrees) |
| `self_sx()` | X scale |
| `self_sy()` | Y scale |
| `self_sz()` | Z scale |

### Writing Position / Rotation / Scale / Color

| Function | Description |
|----------|-------------|
| `self_set_pos(x, y, z)` | Set world position |
| `self_set_rot(rx, ry, rz)` | Set rotation in degrees |
| `self_set_scale(sx, sy, sz)` | Set scale |
| `self_set_color(r, g, b, a)` | Set RGBA color, each component 0–1 |

### Physics

These require the object to have a Rigidbody (mass > 0).

| Function | Description |
|----------|-------------|
| `self_apply_impulse(x, y, z)` | Apply an instantaneous impulse |
| `self_apply_force(x, y, z)` | Apply a continuous force (per frame) |
| `self_set_velocity(x, y, z)` | Set linear velocity directly |
| `self_on_ground()` | `true` if a short ray straight down hits something (jump/ground check) |
| `self_set_friction(f)` | Set this body's friction coefficient (`0` = frictionless) |
| `self_collided()` | `true` if this object is currently touching anything, any direction |
| `self_contact_above()` | The object resting on top of this one, or `null` — see below |
| `self_set_nametag(text, offset, size, r, g, b)` | Float a label above this object; empty text removes it |
| `reset_scene()` | Rebuild the world as it was loaded and return the player to spawn |
| `self_explode(radius, speed, up_bias, spin)` | Detonate at this object's position — see below |

```
on update(dt) {
    if (key("space")) {
        self_apply_impulse(0, 5, 0)
    }
}
```

### Pressure plates

`self_contact_above()` returns a **handle**, not a `true`/`false`. `null` is
already falsy, so it reads the same way in an `if` while also handing you the
thing standing on the object — which is usually what you want:

```
on update(dt) {
    let rider = self_contact_above()
    if (rider) {
        rigidbody_of(rider).vy = 24    // a jump pad
    }
}
```

Reach for it instead of `self_collided()` whenever the object sits on
something. `self_collided()` is true for a touch in *any* direction, so a plate
resting on the floor reads as permanently pressed by the ground under it;
`self_contact_above()` only counts contacts pushing down on the top face, and
only from objects that can move — static world geometry is never a load, so a
plate that sinks into its own floor can't hold itself down.

Give a plate **mass 0**. A dynamic one gets shoved through the floor by whatever
lands on it.

One caveat worth knowing: the plate moving down is what breaks the contact
holding it down — it descends immediately while whatever is on it has to be
accelerated by gravity to follow. Read the contact directly and the plate
oscillates a centimetre or two below rest without ever pressing. Latch it for a
fraction of a second instead; all three scripts below show the pattern.

The scene ships three plates, each self-contained so you can copy one and
rewrite only its action:

| Script | What pressing it does |
|--------|-----------------------|
| `scripts/button.cow` | Launches whatever pressed it straight up |
| `scripts/plate_spawn.cow` | Drops a cube of random size and colour beside itself |
| `scripts/plate_reset.cow` | Calls `reset_scene()` |

`reset_scene()` is **deferred** by the engine: it destroys every entity, and the
call necessarily comes from a script running inside the iteration over them, so
it is carried out once scripts have finished for the frame. The plate that
pressed it does not survive — it is rebuilt from the scene like everything else.

### Explosions

`self_explode()` shoves every dynamic body near this object away from it. All
four arguments are optional:

| Argument | Default | Meaning |
|----------|---------|---------|
| `radius` | `6` | Metres. Nothing beyond this is affected at all. |
| `speed` | `24` | Metres per second added to something standing at the centre. |
| `up_bias` | `0.35` | How far the push tilts upward. `0` is purely radial. |
| `spin` | `8` | Tumble imparted to debris. The player never spins. |

`speed` is a **velocity change, not a force**: a blast adds the same metres per
second to a heavy player as to a light prop, so one number is tuned correctly for
everything at once and you never have to think about mass.

Two behaviours are worth knowing when tuning:

- **Falloff is `1 - (d/r)²`.** Most of the blast's strength is spread across the
  near half of the radius and then drops off sharply, so there is a forgiving
  sweet spot rather than a knife edge, and a distant blast is only a nudge.
- **`up_bias` is what makes rocket jumping work.** A blast that goes off beside
  you at floor level is a purely sideways push into the floor, and friction eats
  it. The upward tilt turns it into a launch.

Static geometry between the blast and a target weakens the push but never
cancels it — a shot that detonates half-buried in the surface it hit would
otherwise be a silent dud.

See `scripts/despawn_after.cow` for the exploding cows the sample scene fires,
including how chain reactions fall out of it for free.

---

## Handles and Components

Engine objects are accessed through **handles** — opaque references returned by built-in functions. Use dot notation to read and write properties on handles.

### Getting Handles

| Function | Returns |
|----------|---------|
| `self()` | Handle to the attached object |
| `transform()` | Transform handle for attached object |
| `transform_of(obj)` | Transform handle for any object handle |
| `rigidbody()` | Rigidbody handle for attached object |
| `rigidbody_of(obj)` | Rigidbody handle for any object handle |
| `camera()` | Handle to the active player camera |

### Spawning Objects

Spawn functions return an **object handle** and accept an optional `(x, y, z)`
position (defaults to `(0, 5, 0)`) plus an optional uniform `scale`.

| Function | Description |
|----------|-------------|
| `spawn_cube(x, y, z, scale)` | Spawns a cube, returns object handle |
| `spawn_cow(x, y, z, scale)` | Spawns a cow mesh, returns object handle |
| `spawn_plane(x, y, z, scale)` | Spawns a plane, returns object handle |

Spawned objects get a random color and are added to the live scene.

`(x, y, z)` is where the object's **visible centre** lands, not its model origin —
a model's origin is wherever the artist left it (`cow.obj`'s is 0.89 units off its
own centre), so aiming at the origin would put a shot fired down the camera's view
axis a couple of degrees beside the crosshair.

Pass `scale` here rather than writing `.sx`/`.sy`/`.sz` afterwards. The spawn can
only place the centre correctly if it knows the final size, and the collision hull
is then built — and its inertia computed — at that size. Setting the scale after
the fact resizes the collider about the model origin, which moves the object.

### Object Lifetime

| Function | Description |
|----------|-------------|
| `destroy(obj)` | Removes the object a handle refers to |
| `destroy_self()` | Removes the entity the running script is attached to |
| `attach_script(obj, path)` | Compiles `path` and attaches it to the object; returns `true` on success |

`attach_script` lets a spawned object run its own behaviour, so the spawner
doesn't have to keep a handle to it and manage it from the outside. The attached
script's `on start()` fires on the object's next frame, and `self_*` /
`destroy_self()` inside it act on that object. Attaching the same path twice is a
no-op.

---

## Handle Properties

Once you have a handle, read and write properties with `.`:

```
let t = transform()
t.x = 0        // write
let y = t.y    // read
```

### Object Handle (`spawn_*`, `self()`)

| Property | Type | Description |
|----------|------|-------------|
| `.x` `.y` `.z` | number (r/w) | World position shortcut |
| `.rx` `.ry` `.rz` | number (r/w) | Rotation shortcut |
| `.sx` `.sy` `.sz` | number (r/w) | Scale shortcut |
| `.transform` | handle (r) | Gets the transform component handle |
| `.rigidbody` | handle (r) | Gets the rigidbody component handle |
| `.name` | string (r) | Object's name |

### Transform Handle (`transform()`, `transform_of()`)

| Property | Type | Description |
|----------|------|-------------|
| `.x` `.y` `.z` | number (r/w) | World position |
| `.rx` `.ry` `.rz` | number (r/w) | Rotation in degrees |
| `.sx` `.sy` `.sz` | number (r/w) | Scale |

### Rigidbody Handle (`rigidbody()`, `rigidbody_of()`)

| Property | Type | Description |
|----------|------|-------------|
| `.vx` `.vy` `.vz` | number (r/w) | Linear velocity |
| `.px` `.py` `.pz` | number (r) | Live body position (fresher than the transform mid-frame) |
| `.mass` | number (r) | Object mass |

### Camera Handle (`camera()`)

| Property | Type | Description |
|----------|------|-------------|
| `.x` `.y` `.z` | number (r/w) | Camera world position |
| `.fx` `.fy` `.fz` | number (r) | Front (forward) direction vector |
| `.rx` `.ry` `.rz` | number (r) | Right direction vector |
| `.ux` `.uy` `.uz` | number (r) | Up direction vector |
| `.yaw` | number (r) | Horizontal rotation in degrees |
| `.pitch` | number (r) | Vertical rotation in degrees |

---

## Examples

### Spin and Bob

Rotates the object on Y and bobs it up and down using `sin`:

```
let base_y = 0
let speed = 60

on start() {
    base_y = self_y()
}

on update(dt) {
    let yaw = self_ry() + speed * dt
    self_set_rot(self_rx(), yaw, self_rz())
    let y = base_y + sin(time() * 2) * 1.5
    self_set_pos(self_x(), y, self_z())
}
```

### Jump on Space

Applies an upward impulse and changes color when Space is pressed:

```
let was_down = false

on update(dt) {
    let down = key("space")
    if (down and not was_down) {
        self_apply_impulse(0, 5, 0)
        self_set_color(random(), random(), random(), 1)
    }
    was_down = down
}
```

### Shoot Cows from the Camera

Spawns a cow in the camera's forward direction, launches it, and hands it a
script that despawns it later. Because the lifetime lives on the cow, any number
of shots can be in flight at once:

```
let was_down = false
let speed = 100

on update(dt) {
    let down = key("c")
    if (down and not was_down) {
        let cam = camera()
        let ox = cam.x + cam.fx * 2
        let oy = cam.y + cam.fy * 2
        let oz = cam.z + cam.fz * 2
        let cow = spawn_cow(ox, oy, oz, 0.1)
        let rb = cow.rigidbody
        rb.vx = cam.fx * speed
        rb.vy = cam.fy * speed
        rb.vz = cam.fz * speed
        attach_script(cow, "scripts/despawn_after.cow")
    }
    was_down = down
}
```

...where `scripts/despawn_after.cow` is simply:

```
let lifetime = 4
let born = 0

on start() {
    born = time()
}

on update(dt) {
    if (time() - born > lifetime) {
        destroy_self()
    }
}
```

### Orbit Around Origin

Makes the object orbit the origin at a fixed radius using trigonometry:

```
let radius = 5
let angle = 0
let orbit_speed = 90

on update(dt) {
    angle = angle + orbit_speed * dt
    let rad = angle * 3.14159 / 180
    self_set_pos(cos(rad) * radius, self_y(), sin(rad) * radius)
}
```

---

## Tips

- Use `dt` (delta time) from `on update(dt)` for all movement to stay frame-rate independent.
- The `was_down` pattern (store previous key state, act only on the rising edge) prevents holding a key from firing every frame.
- `spawn_*` functions return handles — store them in a variable if you need to access the spawned object later.
- `self_apply_impulse` requires the object to have a rigidbody (set Mass > 0 in the Inspector).
- Use `print(...)` in the Debug Console to inspect values while testing.
