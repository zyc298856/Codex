# RK3588 SSH Location Map

## Purpose

This note records which RK3588 SSH address should be used depending on the user's current location.

## Current Mapping

| User location | Preferred RK3588 SSH IP | User | Password | Last verified |
|---|---:|---|---|---|
| 自己家 | `192.168.2.156` | `ubuntu` | `ubuntu` | 2026-04-25 |
| 外婆家 | `192.168.10.186` | `ubuntu` | `ubuntu` | 2026-04-28 |

## Usage Rule

When the user says they are at:

- `自己家`, use `ssh ubuntu@192.168.2.156`
- `外婆家`, use `ssh ubuntu@192.168.10.186`

If SSH fails, rescan the current local subnet and update this file.
