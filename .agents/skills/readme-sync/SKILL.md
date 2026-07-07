---
name: readme-sync
description: >
  After ANY change to code, Dockerfiles, bash scripts, or YAML files,
  the corresponding README.md MUST be updated. Additionally, check and
  update these 4 global docs if affected.
---

## Trigger

This skill applies whenever you modify:

| Trigger | Examples |
|---------|----------|
| C++ source/header | `services/*/server/src/*.cpp`, `common/**/*.h` |
| Dockerfile | `deploy/docker/**/Dockerfile`, `services/*/Dockerfile` |
| Bash scripts | `deploy/docker/**/entrypoint.sh`, `deploy.sh`, `build.sh` |
| YAML | `deploy/k8s/**/*.yaml`, `deploy/docker/docker-compose.yml` |

## Mandatory: Update the corresponding README

Each module has its own README. After changing any file under a module
directory, update the nearest README accordingly:

| Module | README to update |
|--------|-----------------|
| `services/<name>/` | `services/<name>/README.md` |
| `deploy/docker/<name>/` | `deploy/docker/README.md` (unless service has standalone doc) |
| `deploy/k8s/` | `deploy/k8s/README.md` |
| `common/` | (no standalone README; note changes for root README) |
| `proto/` | (no standalone README; note changes for root README) |

## Mandatory: Check 4 Global Docs

After ANY change, review whether these 4 files need updates:

| # | File | Audit scope |
|---|------|------------|
| 1 | `README.md` (project root) | Architecture diagram, quick start, build instructions, ports, service list |
| 2 | `deploy/docker/README.md` | Directory tree, build steps, image list, entrypoint behavior |
| 3 | `deploy/k8s/README.md` | Directory tree, deploy commands, config map keys, parameter docs |
| 4 | `CONFIG.md` (project root) | All gflags, env vars, config map keys referenced in changed files |

If the change introduces a new flag, env var, port, image, or command,
the relevant global doc MUST be updated. If unsure, flag it to the user.

## Checklist

- [ ] Nearest module README updated if applicable
- [ ] `README.md` (root) checked
- [ ] `deploy/docker/README.md` checked
- [ ] `deploy/k8s/README.md` checked
- [ ] `CONFIG.md` checked
- [ ] New flags/env-vars/ports documented in all relevant places
