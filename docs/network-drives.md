# Network drives

MDEditor opens any file the system can see as a folder, so a network or cloud drive works once it appears as one.
Connect it with the tool below, then use **File → Open Folder** on it.

| Drive | How to connect |
|---|---|
| Google Drive | **File → Google Drive…** in MDEditor (below), or [Google Drive for desktop](https://www.google.com/drive/download/) on Windows and macOS. |
| Dropbox, OneDrive | Their desktop apps, which keep a local folder in sync. |
| Windows or NAS share (SMB) | Windows: *Map network drive*. Linux: open `smb://server/share` in the file manager. |
| SFTP, FTP/FTPS | Linux: open `sftp://user@server/path` in the file manager. On any system: rclone (below). |

Prefer SFTP or FTPS. Plain FTP sends the password and your files unencrypted.

## Google Drive in MDEditor

**File → Google Drive…** connects a Drive folder without using the terminal. It needs
[rclone](https://rclone.org/install/); the `.deb` package installs it by default. On Windows, also install
[WinFsp](https://winfsp.dev).

1. Click **Sign In with Google…** and allow access in your browser.
2. Enter the **Folder on Google Drive**, such as `Notes`, or leave it empty for the whole drive.
3. Pick an empty **Local folder**, then click **Connect**.

**The connection is remembered.** MDEditor reconnects when it starts, and files that were open reopen once the drive
is up. Updating or reinstalling MDEditor changes nothing, because the settings are in your profile, not in the
application: `~/.config/MDEditor` and `~/.config/rclone` on Linux, the registry and `%APPDATA%\rclone` on Windows.

Good to know:

- The drive stays connected after you close MDEditor, so a file you just saved finishes uploading. **Disconnect**
  ends it, and logging out of your computer ends it too.
- **Sign Out** removes the login from this computer. To also revoke it, remove rclone in your
  [Google account's third-party access](https://myaccount.google.com/connections).
- rclone stores the login in its configuration file, readable by your user account. `rclone config` can encrypt it,
  but then rclone needs the password on every start, which this connection doesn't support.
- MDEditor signs in with rclone's shared Google access, with full access to your Drive so it can see your existing
  notes.
- Changes from other computers can take a minute or two to appear.

## Any drive with rclone

[rclone](https://rclone.org) connects to FTP, SFTP, Google Drive and many others. `rclone config` asks for the
account details once. Then mount a specific folder:

```bash
rclone config
rclone mount gdrive:Notes ~/Notes --vfs-cache-mode writes
rclone mount myserver:/documents/notes ~/Notes --vfs-cache-mode writes
```

`--vfs-cache-mode writes` is needed: without it, the way applications save files safely doesn't work. Mounting needs
FUSE on Linux and [WinFsp](https://winfsp.dev) on Windows.

## What MDEditor does for you

- **Changes made elsewhere.** When you come back to the window, tabs without unsaved changes reload by themselves. If
  you have unsaved changes, a bar lets you reload or keep your version.
- **Saving over someone else's changes.** If the file changed since you opened it, saving asks first: overwrite, save
  under another name, or discard your version. Reload is undoable with Ctrl+Z.
- **Slow connections.** The window keeps working while a file is read or written. After a moment a dialog offers
  **Stop Waiting**. A write that has already started can't be cancelled, so check the file afterwards.
- **Disconnected drives.** Saving fails with a message and your text stays in the tab. Use **Save As** to keep it
  somewhere else.

## Limitations

- Changes are detected when you return to the window and when you save, not while you are typing in MDEditor.
- Two people editing at once are warned on save, but changes are not merged.
- Images in a note are still loaded directly, so a note with many images on a slow drive can pause while opening.
- On drives with coarse timestamps, a change that keeps the same size within the same timestamp can go unnoticed.
