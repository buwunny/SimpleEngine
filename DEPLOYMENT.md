# Deploying CowEngine multiplayer

There are two independent deployables:

1. **The site** — static WASM builds published to **GitHub Pages** (the landing
   page at `/`, the game at `/play`, the editor at `/edit`).
2. **The backend** — the headless C++ **server** + the transport **sidecar**,
   hosted on any box that allows inbound `443/tcp` (and `4443/udp` for
   WebTransport). The static site connects to it over `wss://`.

```
Browser (https://cowengine.com/play)
        │  wss://game.cowengine.com   (WebTransport primary → WebSocket fallback)
        ▼
  Sidecar  ──local UDP──►  C++ server        (both on your host, via docker compose)
 (TLS term)   4433/udp     (authoritative sim)
```

The browser page is served from `https://`, so it can only open a **secure**
socket (`wss://`) — that's why the sidecar must terminate TLS. `ws://` works only
for local dev from `http://localhost`.

---

## 1. Backend: server + sidecar

### Prerequisites
- A host (VPS/Fly.io/etc.) with a public IP and Docker + Docker Compose.
- Firewall/security group open for **inbound `443/tcp`** and **`4443/udp`**.
- A DNS record: `game.cowengine.com` → your host's IP.
- A TLS certificate for `game.cowengine.com` (Let's Encrypt below).

There are two ways to run it:

| | Build on the host | **Ship prebuilt images** (recommended) |
| --- | --- | --- |
| Compose file | `deploy/docker-compose.yml` (`build:`) | `deploy/docker-compose.prod.yml` (`image:`) |
| Host needs | full toolchain, vcpkg, ~15 min, >2 GB RAM | Docker only |
| How | `docker compose up --build -d` | `deploy/deploy.sh user@host` |

A small VPS will spend 10–20 minutes (and can OOM) compiling Bullet + vcpkg, so
prefer the second column. **Don't copy bare binaries** — the server links your
workstation's glibc/libstdc++ and won't run on the VPS's older ones; the image
carries its own userland, which is the whole point.

### 1a. Provision the VPS (OVHcloud)

Any VPS works; these are the OVH-specific bits.

1. **Order** — VPS-2 (2 vCPU / 4 GB) or larger, **Debian 12** or **Ubuntu 24.04**,
   with your SSH key. OVH VPS instances are x86_64, which matches what
   `deploy.sh` builds (`--platform linux/amd64`).
2. **DNS** — in the OVH Control Panel → *Domains* → your zone, add an `A` record
   `game` → the VPS IPv4 (and `AAAA` → its IPv6 if you want). If the domain is
   registered elsewhere, add it there instead. Also set the VPS's **reverse DNS**
   to `game.cowengine.com` (VPS → *IPs* → edit reverse) — not required, but it
   keeps TLS/abuse tooling happy.
3. **OVH Network Firewall** — leave it **disabled** unless you need it. It is
   *stateless*, so enabling it means writing explicit rules for return traffic
   and it is a common cause of "wss connects but QUIC/UDP doesn't". Filter on the
   host with `ufw` instead. If you do enable it, permit `tcp/22`, `tcp/443`,
   `udp/4443` inbound plus established TCP.
4. **Anti-DDoS (VAC)** is always on and can't be turned off. Under mitigation it
   may drop unsolicited UDP, which only affects the WebTransport path — clients
   fall back to `wss://` automatically.
5. **Harden + firewall on the host:**
   ```bash
   ssh ubuntu@<vps-ip>
   sudo apt update && sudo apt upgrade -y
   sudo ufw allow OpenSSH
   sudo ufw allow 80/tcp           # certbot HTTP-01 challenge (see 1b)
   sudo ufw allow 443/tcp          # wss
   sudo ufw allow 4443/udp         # WebTransport (HTTP/3)
   sudo ufw --force enable
   ```
   > Docker publishes ports by writing its own iptables rules that bypass `ufw`,
   > so the container ports are reachable regardless — `ufw` here protects
   > *everything else* on the box. Keep the server container unpublished (the
   > prod compose file already does) so `4433/udp` is never exposed.
6. **Install Docker** (official repo, not the distro's old `docker.io`):
   ```bash
   curl -fsSL https://get.docker.com | sudo sh
   sudo usermod -aG docker "$USER" && exit   # re-login for the group to apply
   ```

### 1b. Get a certificate (Let's Encrypt)

On the VPS, before the stack is running (nothing may hold `:80`/`:443`):
```bash
sudo apt install -y certbot
sudo certbot certonly --standalone -d game.cowengine.com
# → /etc/letsencrypt/live/game.cowengine.com/{fullchain.pem,privkey.pem}
```

`--standalone` binds `:80` for the HTTP-01 challenge, both now **and on every
renewal** (certbot's systemd timer runs unattended twice a day). Inbound `80/tcp`
therefore has to stay open, or renewal fails silently and the cert expires 90 days
later. Nothing in this stack ever listens on `:80` otherwise — certbot binds it for
a few seconds and releases it — so an open-but-unserved port costs you scan noise
and nothing else.

Prefer not to leave it open? Two alternatives:
- **Open it only for the renewal**, via certbot hooks:
  ```bash
  sudo certbot renew --pre-hook 'ufw allow 80/tcp' --post-hook 'ufw delete allow 80/tcp'
  ```
  (put the flags in `/etc/letsencrypt/renewal/game.cowengine.com.conf` to make the
  timer use them too, otherwise they only apply to that manual run).
- **Use DNS-01 and no port at all** — if the domain's DNS is at OVH,
  `sudo apt install python3-certbot-dns-ovh` plus an API credentials file lets you
  issue with `--dns-ovh`. More setup, but nothing inbound and wildcards work.

Verify renewal actually works end to end — this exercises the firewall, the
challenge, and the deploy hook:
```bash
sudo certbot renew --dry-run
```

**Do not mount `/etc/letsencrypt` into the sidecar.** Certbot keeps `live/` and
`archive/` at `0700 root:root`, the sidecar runs unprivileged, and the failure is
quiet — it logs `Permission denied`, disables TLS, and serves plain `ws://`, which
an https page cannot connect to. Publish a copy the container can read instead,
with `install-certs.sh`.

Both deploy paths copy that script to `/opt/cowengine/`, so after your first
deploy attempt it's already on the host. To run it *before* deploying anything,
fetch it directly:

```bash
sudo mkdir -p /opt/cowengine
sudo curl -fsSL -o /opt/cowengine/install-certs.sh \
  https://raw.githubusercontent.com/<you>/CowEngine/main/deploy/install-certs.sh
sudo chmod +x /opt/cowengine/install-certs.sh
```

Then publish the cert (safe to run before the stack exists — it just skips the
sidecar reload):

```bash
sudo /opt/cowengine/install-certs.sh game.cowengine.com
# → /opt/cowengine/tls/{fullchain.pem,privkey.pem}, owned by uid 10001
```

That's the `TLS_DIR` the compose files mount. The sidecar's uid is pinned to
**10001** in `Dockerfile.sidecar` precisely so the host can chown to it; don't
change one without the other.

Install the same script as a certbot deploy hook so renewals keep working — it
re-copies the PEMs and restarts the sidecar (which only reads them at startup):

```bash
sudo cp /opt/cowengine/install-certs.sh /etc/letsencrypt/renewal-hooks/deploy/cowengine.sh
sudo chmod +x /etc/letsencrypt/renewal-hooks/deploy/cowengine.sh
```

### 1c. Ship the images from your workstation

`deploy/deploy.sh` builds both images locally, streams them over SSH
(`docker save | gzip | ssh docker load` — no registry account needed), installs
`docker-compose.prod.yml` + `.env` under `/opt/cowengine`, and restarts the stack.

```bash
deploy/deploy.sh ubuntu@game.cowengine.com
```

The first run stops after seeding `/opt/cowengine/.env` (exit 3). Review it on the
VPS, then rerun the same command:

```bash
ssh ubuntu@game.cowengine.com 'nano /opt/cowengine/.env'
# TLS_DIR=/opt/cowengine/tls        (what install-certs.sh populates — not /etc/letsencrypt)
# COW_ALLOWED_ORIGINS=https://cowengine.com
# COW_JOIN_KEY=                     (set for friends-only)
deploy/deploy.sh ubuntu@game.cowengine.com
```

Images are tagged with the current git short SHA and pinned into `.env`, so
`docker images` on the host is a deploy history and rollback is a one-line edit
plus `docker compose -f docker-compose.prod.yml up -d`.

Useful flags:

| Flag | Effect |
| --- | --- |
| `--sidecar-only` / `--server-only` | Build+ship one image (the sidecar is ~1 min, the server ~10). |
| `--no-build --tag <sha>` | Re-point the host at an image it already has. |
| `--registry ghcr.io/you` | Push/pull through a registry instead of streaming over SSH. Do this once CI builds the images. |
| `--remote-dir /srv/cow` | Somewhere other than `/opt/cowengine`. |

### 1d. Automatic deploys from GitHub Actions (optional)

[`Deploy backend to VPS`](.github/workflows/backend.yml) does the same thing from
CI: builds both images, pushes them to **GHCR**, then SSHes to the VPS to pull and
restart. It runs on **push to `main`** (only when backend paths change) and on
manual dispatch — the dispatch form has an *images* choice so you can redeploy
just the sidecar.

Set these in **Settings → Secrets and variables → Actions → Secrets**:

| Secret | Value |
| --- | --- |
| `VPS_HOST` | `game.cowengine.com` (or the IP) |
| `VPS_USER` | `ubuntu` |
| `VPS_SSH_KEY` | A **deploy-only** private key. Generate with `ssh-keygen -t ed25519 -f cow-deploy -N ''`, append `cow-deploy.pub` to the VPS's `~/.ssh/authorized_keys`, paste `cow-deploy` here. |
| `VPS_KNOWN_HOSTS` | *(optional)* `ssh-keyscan game.cowengine.com` output — pins the host key instead of trusting on first use. |
| `GHCR_PULL_TOKEN` | *(optional)* `read:packages` PAT, only if the GHCR packages stay private. |

Two more things:
- **Seed `/opt/cowengine/.env` once** before the first CI deploy (run
  `deploy/deploy.sh` from your workstation, or `cp .env.example .env` on the host
  and run `install-certs.sh`). The workflow refuses to start a stack with no `.env`
  rather than guessing your cert path; it only ever rewrites the two
  `COW_*_IMAGE` lines, so your join key and origins survive every deploy.
- **Make the two GHCR packages public** after the first push (Packages → package
  → *Package settings* → *Change visibility*) so the VPS pulls without
  credentials. Otherwise set `GHCR_PULL_TOKEN`.

The job targets a `production` environment, so you can add required reviewers
under **Settings → Environments → production** if you'd rather approve each
deploy. Images are tagged with the commit SHA (plus `latest`), previous images
are kept for a week, so a rollback is editing `COW_SERVER_IMAGE` in `.env` and
re-running `up -d`.

> This workflow is independent of the Pages one — the site and the backend
> deploy separately. Bumping `kProtocolVersion` means you must ship **both**, or
> the version check refuses every client.

### 1e. What's running
- `server` — internal only, no published ports; the sidecar reaches it over the
  compose network at `server:4433`.
- `sidecar` — publishes `443:8080` (wss) and `4443:4443/udp` (WebTransport), and
  mounts `TLS_DIR` read-only at `/tls` for `COW_TLS_CERT`/`COW_TLS_KEY`.

Check it:
```bash
ssh ubuntu@game.cowengine.com \
  'cd /opt/cowengine && docker compose -f docker-compose.prod.yml logs -f sidecar'
# expect: "cowengine-sidecar: WSS wss://0.0.0.0:8080  ->  udp 127.0.0.1:4433"
```

> **Cert renewal:** Let's Encrypt certs last 90 days. `certbot renew` needs `:80`,
> which nothing here uses, so it renews unattended — provided you installed
> `install-certs.sh` as the deploy hook in [1b](#1b-get-a-certificate-lets-encrypt).
> The hook re-copies the new PEMs into `TLS_DIR` and restarts the sidecar; without
> it, renewal succeeds but the sidecar keeps serving the expired cert.

> **Building on the host anyway?** Clone the repo there and use the original
> `deploy/docker-compose.yml` with `docker compose up --build -d` (after
> `install-certs.sh`).
> Give it swap first (`fallocate -l 4G /swapfile`…) — the C++ build is the part
> that OOMs on a 2 GB VPS.

---

## 2. Frontend: GitHub Pages

The [`Deploy static content to Pages`](.github/workflows/static.yml) workflow
builds `game-web` + `editor-web`, assembles the site, bakes in the server URL,
and publishes. It runs on **manual dispatch** (Actions → run workflow).

### One-time setup
In **Settings → Secrets and variables → Actions → Variables**, add:

| Variable | Example | Purpose |
| --- | --- | --- |
| `COWENGINE_SERVER_WS` | `wss://game.cowengine.com` | Baked into `/play` as the server. **Required for multiplayer.** |
| `COWENGINE_SERVER_WT` | `https://game.cowengine.com:4443` | Optional WebTransport URL (primary; WS is the fallback). |
| `COWENGINE_API` | `https://game.cowengine.com` | Control-plane base, baked into `/`, `/play` and `/edit`. Without it the game browser, the projects dialog and Build → Publish are all disabled. Defaults to the host of `COWENGINE_SERVER_WS`. |
| `PAGES_CNAME` | `cowengine.com` | Optional custom domain (writes `site/CNAME`). |

In **Settings → Pages**: set **Source = GitHub Actions**, and (if using a custom
domain) set the custom domain to match `PAGES_CNAME`.

### Publish
Run the workflow (Actions → *Deploy static content to Pages* → *Run workflow*).
Result:
- `https://cowengine.com/` — landing page
- `https://cowengine.com/play/` — networked game (auto-connects to the baked server)
- `https://cowengine.com/edit/` — editor

If `COWENGINE_SERVER_WS` is unset, `/play` is published single-player; players can
still opt in with `?ws=wss://host` / `?wt=https://host:4443` query params.

### Projects and project keys

**Editor** on the landing page opens a projects dialog rather than going
straight to `/edit/`. From there you can start a new project, reopen one this
browser has published before, or open any project by pasting its **project
key**:

```
<project id>.<edit key>       e.g. moon-ranch-a1b2c3.9f3c…7ab1
```

The id is the one the server assigned on publish; the edit key is the secret it
showed exactly once, in the editor's publish dialog. Together they let an author
carry a project to another browser or machine — the dialog downloads the current
bundle into the editor and files the key away so Build → Publish updates that
project instead of creating a new one. `?project=<project key>` on the landing
page is the same thing as a link.

Pasting only the id (no edit key) opens the project as a **copy**: it loads, but
publishing creates a new project, because updates need the key. There is no
recovery path for a lost edit key — the server stores only its hash.

---

## Access control & bandwidth

The server sends snapshots at **20 Hz** (physics still runs at 60 Hz; clients
interpolate, so it's invisible) and omits velocity from replicated entities —
together roughly a **4× cut** in egress vs. the naive 60 Hz full state.

The sidecar gates every connection (WS **and** WebTransport) via env vars — none
of this is strong auth (the web client is public), but it keeps casual abuse and
resource exhaustion out:

| Var | Example | Effect |
| --- | --- | --- |
| `COW_ALLOWED_ORIGINS` | `https://cowengine.com` | Reject connections whose `Origin` isn't listed (comma-separated). Empty = allow any. |
| `COW_JOIN_KEY` | `s3cret` | Require `?key=s3cret` on the connect URL. **For a friends-only server, set this and don't bake it into the published page** — hand out `…/play/?key=s3cret` instead. Empty = off. |
| `COW_MAX_CONN_PER_IP` | `8` | Cap concurrent connections per client IP (0 = unlimited). |

For a friends-only deploy: set `COW_JOIN_KEY`, **leave `COWENGINE_SERVER_WS` unset**
in the Pages workflow (so the public page ships single-player with no server baked
in), and share a magic link `https://cowengine.com/play/?ws=wss://game.cowengine.com&key=s3cret`.

## 3. Verification checklist
- [ ] `dig game.cowengine.com` resolves to the host IP.
- [ ] `openssl s_client -connect game.cowengine.com:443` shows the real cert.
- [ ] `docker compose -f docker-compose.prod.yml ps` on the host shows both
      services `running` (the server should never show a published port).
- [ ] Sidecar log shows `WSS wss://...`; server log shows `listening ... (scene: ...)`.
- [ ] Open `https://cowengine.com/play` in **two** browsers → each sees the other
      move, and shot cows / scene objects stay in sync.
- [ ] Reload a tab → it reconnects cleanly and no ghost avatar lingers.
- [ ] `sudo certbot renew --dry-run` succeeds — proves `:80` is reachable and the
      deploy hook republishes the cert. A cert that can't renew takes the game
      offline 90 days later, with no warning before it does.
- [ ] DevTools console shows `[CowNet] WebSocket connected wss://…` (or
      `WebTransport connected`).

---

## Local dev (no TLS)
Plain `ws://` from `http://localhost` needs no certificate:
```bash
# 1) server
cmake --preset server-native && cmake --build --preset server-native
./build/server-native/CowEngineServer            # binds udp 4433

# 2) sidecar (WS only, fast build)
cargo build --manifest-path sidecar/Cargo.toml
./sidecar/target/debug/cowengine-sidecar         # ws://0.0.0.0:8080

# 3) game, pointed at the local sidecar
cmake --preset game-web && cmake --build --preset game-web && cmake --install build/game-web
python3 -m http.server -d dist/game-web 5500
# open http://localhost:5500/?ws=ws://localhost:8080 in two tabs
```
Build the sidecar with `--features tls` (+ `COW_TLS_CERT`/`COW_TLS_KEY`) for
`wss://`, and add `--features webtransport` for the HTTP/3 path.

> **Tip:** if connections silently fail, kill stale processes first —
> `pkill -f CowEngineServer; pkill -f cowengine-sidecar` — a leftover server
> holds the ports and absorbs connections.

---

## Known caveats
- **WebTransport cert:** when `COW_TLS_CERT`/`COW_TLS_KEY` are set (they are, in
  `docker-compose.yml`), the WebTransport listener loads the **same CA-signed PEM**
  as `wss://`, so the browser validates it normally — no `serverCertificateHashes`
  needed. Without those env vars it falls back to a self-signed dev cert and prints
  a `?certhash=` to pin. If UDP/4443 is blocked, clients still connect over `wss://`.
- **Single room:** one shared world, capacity `maxPlayers_ = 16` (server-side).
- **UDP for HTTP/3:** WebTransport needs inbound **UDP** 4443 open; some hosts
  block UDP by default — if so, the game still works over `wss://`.
