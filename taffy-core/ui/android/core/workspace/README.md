# `:core:workspace`

**Status:** `[Current]` Android's workspace projection and typed mutation port
over the generated browser Core API.

Android never fabricates or formats workspace content. The module projects
immutable Rust-owned state, carries current revisions on mutations, and accepts
only exact Rust export completions. `WorkspaceBindings` owns the profile scope.
