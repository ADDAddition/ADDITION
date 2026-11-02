# Android release signing

Release builds are signed with the key described by `android/key.properties`
(git-ignored, never commit it or the keystore):

```
storeFile=/home/<you>/.addition-keys/addition-release.jks
storePassword=...
keyPassword=...
keyAlias=addition
```

Keep the keystore and passwords offline-backed-up (encrypted). Losing them means
the app can no longer be updated under the same identity. Without key.properties
the release build falls back to the debug key: test-only, never distribute it.
