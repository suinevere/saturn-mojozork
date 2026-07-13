# Hosting the MultiZork server (Docker)

This directory containerizes **`multizorkd`** — the multiplayer Zork telnet
server that **Play Online** connects to. The image is **self-contained**: it
clones the source from GitHub and builds it at image-build time, so the host
running it needs **no local checkout** — just Docker and these two files
(`Dockerfile`, `docker-compose.yml`).

Our live instance runs on an **Oracle Cloud Free Tier** VM and is reachable at
**`suinevere.duckdns.org`** (telnet port 23, with 2323 as an alternate).

---

## What the image does

- **Build stage** installs `git` + a C toolchain + `libsqlite3-dev`, clones
  `suinevere/zaturn` (branch `main`), and compiles `saturn/multizorkd.c`
  (which `#include`s `mojozork.c`) against system SQLite.
- **Runtime stage** ships only the `multizorkd` binary and `ZORK1.Z3`, runs as a
  **non-root** user, and serves telnet on container port **2323**.
- `multizork.sqlite3` (game instances + transcripts) is written to `/data`, kept
  on a named volume so it survives restarts.

> MultiZork is **Zork 1-specific** — `multizorkd.c` contains hardcoded Zork 1
> story-address patches — so `ZORK1.Z3` is the only story it can serve.

### Build args (override with `--build-arg`)

| Arg | Default | Purpose |
|---|---|---|
| `REPO_URL` | `https://github.com/suinevere/zaturn.git` | source to clone |
| `REPO_REF` | `main` | branch/tag to build |

A `docker compose build` picks up new commits automatically (a GitHub API
cache-bust invalidates the clone layer); force a clean rebuild with
`docker compose build --no-cache`.

---

## Quick start (any Docker host)

```bash
git clone https://github.com/suinevere/zaturn.git
cd zaturn/docker
docker compose up -d --build
docker compose logs            # expect: "Now accepting connections on port 2323"
telnet localhost 23            # "Hello sailor!" = working
```

`docker-compose.yml` publishes **host 23 and 2323** (both map to container 2323),
uses `restart: unless-stopped`, and persists state in the `multizork-data` volume.
Some client ISPs block outbound port 23, so 2323 gives those players a way in.

---

## Production hosting: Oracle Cloud Free Tier + DuckDNS

How the live `suinevere.duckdns.org` instance is set up, end to end.

### 1. On the instance

```bash
curl -s ifconfig.me; echo                     # note the public IPv4

# deploy
sudo apt-get update && sudo apt-get install -y docker.io docker-compose-plugin git
git clone https://github.com/suinevere/zaturn.git
cd zaturn/docker
sudo docker compose up -d --build
```

Open the **host firewall** — Oracle images block everything but SSH by default.
Check the OS with `cat /etc/os-release`, then:

- **Ubuntu** (uses iptables, not ufw, by default):
  ```bash
  sudo iptables -I INPUT 6 -m state --state NEW -p tcp --dport 23   -j ACCEPT
  sudo iptables -I INPUT 6 -m state --state NEW -p tcp --dport 2323 -j ACCEPT
  sudo netfilter-persistent save
  ```
- **Oracle Linux / RHEL** (firewalld):
  ```bash
  sudo firewall-cmd --permanent --add-port=23/tcp --add-port=2323/tcp
  sudo firewall-cmd --reload
  ```

Confirm it listens locally before touching DNS: `telnet localhost 23`.

### 2. Oracle Cloud Console — open the ports at the network layer

The host firewall isn't enough; OCI's virtual network blocks inbound too.

1. Console **☰ → Networking → Virtual Cloud Networks** → your **VCN**.
2. **Resources → Security Lists** → the **Default Security List** for your subnet.
3. **Add Ingress Rules**, one per port (23 and 2323):
   - Stateless: **unchecked**
   - Source Type: **CIDR**, Source CIDR: `0.0.0.0/0`
   - IP Protocol: **TCP**
   - Source Port Range: *(blank)*
   - Destination Port Range: `23` (repeat for `2323`)
4. **Add Ingress Rules**.

(If your instance uses a Network Security Group instead, add the same rules under
**Networking → Network Security Groups → your NSG**.)

### 3. DuckDNS — free domain pointing at the instance

1. Go to **duckdns.org**, sign in (GitHub / Google / etc.).
2. Add a subdomain — ours is `suinevere` → `suinevere.duckdns.org`.
3. Paste the instance's **public IPv4** into the **current ip** box → **update ip**.
   (Static IP = set it once; no dynamic-DNS updater needed.)

### 4. Verify from another machine

```bash
nslookup suinevere.duckdns.org      # returns the Oracle IP
telnet   suinevere.duckdns.org 23   # "Hello sailor!"
```

DNS may take a few minutes the first time. If it resolves but telnet hangs, it's
always one of the two firewall layers (step 1 or 2).

---

## Routing the Saturn NetLink dial code to this server (DreamPi)

**Play Online** dials NetLink into a **DreamPi** running the eaudunord Netlink
tunnel, which relays dial code `199403` to a telnet server. Point that code at
this deployment by editing the DreamPi's `/dreampi/netlink_config.ini`:

```ini
[server:199403]
name = MultiZork
host = suinevere.duckdns.org
port = 23
handler = transparent
```

`handler = transparent` is required — multizork does no AUTH handshake. See the
top-level [README](../README.md) section *Playing online from a real Saturn* for
the full DreamPi steps.

---

## Operations

```bash
docker compose logs -f                 # live server log
docker compose restart                 # restart
docker compose build --no-cache && docker compose up -d   # rebuild latest source
docker compose down                    # stop (keeps the volume)
docker volume rm docker_multizork-data # wipe all game state + transcripts
```
