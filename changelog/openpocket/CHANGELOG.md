# openpocket

## 1.0.0-beta.1

### Patch Changes

- [#37](https://github.com/VinSpee/undertone/pull/37) [`de83bb9`](https://github.com/VinSpee/undertone/commit/de83bb9db8065b3b7e9ef10a6aa751cf04f5c8cc) Thanks [@VinSpee](https://github.com/VinSpee)! - Saving or deleting a NAM model or a cabinet IR from the editor no longer reads the pedal's memory while the second processor may be working, which could stop that processor until the pedal was switched off.

- [#38](https://github.com/VinSpee/undertone/pull/38) [`67b29c5`](https://github.com/VinSpee/undertone/commit/67b29c508e81c2f133eb495a8b49837befac0053) Thanks [@VinSpee](https://github.com/VinSpee)! - The editor no longer misses a change made on the pedal when the pedal answers one of its requests within 40 ms of the change.
