#pragma once

// Registers a memory-backed SQLite VFS as the default one, so the game can open
// its database even on runtimes where the bundled SQLite cannot touch files.
// Opt-in via the "bpp_sqlite_shim" marker file.
bool InstallSqliteShim();
