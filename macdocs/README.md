# macdocs/ — documentation handoff inbox (Mac → H&M → docs/)

This directory is a **git-tracked staging area** for documentation source
authored on the (temporary) Intel MacBook environment. It exists because
the MacBook cannot run the Windows-side **H&M documentation app**, and the
published `docs/` tree must never be edited directly.

## Why not write to `docs/` directly?

`docs/` (project root) is **generated OUTPUT** — the H&M app publishes the
XML-styled documentation there. Editing `docs/` by hand loses the work on
the next H&M regeneration. Treat `docs/` as **read-only**.

## Lifecycle of a doc (git is the bridge)

1. **Author** the content here in `macdocs/` (Markdown by default; H&M
   converts to the `docs/` XML style on the Windows side).
2. **Commit + push** to the SSOT (GitHub `main`).
3. The **Windows PC pulls**, and the H&M app **integrates** the content
   into the XML documentation system, which publishes to `docs/`.
4. Once integrated, the source file is **`rm`'d from `macdocs/` on
   Windows** and that deletion is committed + pushed.
5. The **Mac pulls** the deletion and the file disappears here too.

An **empty `macdocs/`** (only this `README.md` remaining) means every
authored doc has been integrated. This `README.md` is the directory's
charter — it is **not** a document to integrate, and should not be removed.

## Rules

- Never edit `docs/` directly — it is H&M output.
- Only documentation **source** lives here; no build artifacts.
- Keep each doc a self-contained `.md` so H&M can integrate it
  independently and the post-integration `rm` is a clean single-file
  delete.
