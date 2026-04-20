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
