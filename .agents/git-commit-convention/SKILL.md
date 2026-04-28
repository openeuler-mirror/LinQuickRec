---
name: git-commit-convention
description: >
  This project follows the Conventional Commits specification. All commit
  messages MUST use the <type>: <subject> format. AI-assisted commits MUST
  include the "AI-assisted: <tool>" footer. Use this skill as a reference.
---

## Commit Message Format

```
<type>: <short summary>

<body (optional)>

<footer (optional)>
```

- Subject: max 50 characters, lowercase, no trailing period
- Body: wrap at 72 characters, explain what and why (not how)

## Allowed Types

| Type       | Usage                                    |
|------------|------------------------------------------|
| `feat`     | New feature                              |
| `fix`      | Bug fix                                  |
| `build`    | Build system, dependencies, CMake/Docker |
| `chore`    | Maintenance, renaming, cleanup           |
| `docs`     | Documentation only                       |
| `refactor` | Code restructuring, no behavior change   |
| `delete`   | Removing files                           |

## AI-Assisted Commits

If a commit was produced with AI assistance, append a footer:

```
AI-assisted: opencode
```

The commit message must still be human-readable and follow all other rules
above; the footer is supplementary metadata only.

## Full Example

```
build: remove unused gRPC dependency from proxy service

All RPC communication in the proxy service uses brpc (Baidu RPC framework),
not gRPC. The gRPC::grpc++ library was linked but never used — no gRPC
headers, functions, or service stubs appear anywhere in the source code.

AI-assisted: opencode
```

## Quick Checklist

- [ ] Type is one of: feat, fix, build, chore, docs, refactor, delete
- [ ] Subject is 50 characters or fewer
- [ ] Subject starts with lowercase, no period at end
- [ ] Body (if any) wrapped at 72 characters
- [ ] AI-assisted commits include the `AI-assisted: <tool>` footer
