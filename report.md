# Lab Report - PES1UG24CS117

## Phase 1: Object Store
- Implemented object_write with SHA-256 hashing, atomic writes, directory sharding
- Implemented object_read with integrity verification

## Phase 2: Tree Objects  
- Implemented tree_from_index with recursive subtree construction
- Handles nested paths like src/main.c correctly

## Phase 3: Staging Area
- Implemented index_load, index_save, index_add
- Uses atomic writes with fsync for durability

## Phase 4: Commits
- Implemented commit_create linking tree, parent, author and HEAD
- commit_walk traverses full history

## Phase 5: Branching and Checkout Analysis

### Q5.1: Implementing pes checkout
To implement `pes checkout <branch>`:
- **Files that change in .pes/:** Update `HEAD` to point to the new branch: `ref: refs/heads/<branch>`
- **Working directory:** Read the target branch's commit, get its tree, then for each file in the tree, write the blob contents to disk
- **Complexity:** If a file exists in the current branch but not the target, it must be deleted. If it exists in both but differs, it must be overwritten. This requires comparing two full trees — the current HEAD tree and the target tree — and applying the diff to the working directory

### Q5.2: Detecting dirty working directory
To detect conflicts before checkout:
1. For each file in the index, compare its `mtime` and `size` against the actual file on disk
2. If they differ, the file is modified (dirty)
3. Also check if the file differs between the two branches by comparing blob hashes
4. If a file is dirty AND differs between branches → refuse checkout and warn the user
5. This uses only the index (staged metadata) and object store (blob hashes) — no diff needed

### Q5.3: Detached HEAD
- In detached HEAD state, `HEAD` contains a commit hash directly instead of `ref: refs/heads/branch`
- New commits are made but no branch pointer is updated, so they are unreachable once you switch away
- **Recovery:** Run `git log` before switching to note the hash, then create a branch: `git branch recovery-branch <hash>` to make the commits reachable again

## Phase 6: Garbage Collection Analysis

### Q6.1: Finding unreachable objects
Algorithm:
1. Start from all branch refs in `.pes/refs/heads/`
2. For each branch, walk the commit chain following parent pointers
3. For each commit, mark its tree hash as reachable
4. For each tree, recursively mark all blob and subtree hashes as reachable
5. Collect all object hashes from `.pes/objects/` using find
6. Delete any object whose hash is NOT in the reachable set
- **Data structure:** A hash set (e.g. C `uthash` or Python `set`) for O(1) lookup
- **Estimate:** 100,000 commits × ~3 objects each = ~300,000 objects to visit across 50 branches

### Q6.2: GC race condition
Race condition scenario:
1. GC scans all objects and builds the reachable set — does NOT yet see the new blob
2. A concurrent `pes add` writes a new blob object to the store
3. A concurrent `pes commit` is about to reference that blob in a tree
4. GC deletes the blob because it wasn't reachable at scan time
5. The commit now references a deleted object → corruption

**How Git avoids this:**
- Git uses a "grace period" — objects newer than 2 weeks are never deleted by GC
- This ensures any in-progress operations that created objects recently are safe
- Git also uses lock files to prevent concurrent GC runs
