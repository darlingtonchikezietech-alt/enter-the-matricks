# AGENTS.md (game repo)

The full protocol is in the **tracker repo**: `../enter-the-matricks-tracker/AGENTS.md`. Read its `RESUME_HERE.md` first.

Rules that apply here:
1. Claim a task in the tracker before editing anything: `python3 tools/agent.py next` → `claim T-xxxx --push`.
2. Edit only the paths your task owns. `.githooks/pre-commit` rejects anything else.
3. Commit as `[T-xxxx] summary` on branch `agent/<you>/<task>`. Open a PR into `main`.
4. `.uasset`/`.umap` files go through Git LFS and can't be merged. Never touch one another agent owns.
5. Report what you *actually* verified (compiled, editor-tested, on-device) when you `complete` the task.
6. No franchise IP in names, assets or comments (bible §2).
