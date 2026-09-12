import { createSnapshotStore } from '../lib/snapshot-store'

let store: ReturnType<typeof createSnapshotStore> | undefined

export function snapshotStore() {
  return store ??= createSnapshotStore(useRuntimeConfig().dataDir)
}
