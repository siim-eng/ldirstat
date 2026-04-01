# LDirStat Help

LDirStat analyzes disk usage so you can see what is taking up space on your drives.

## Welcome Screen

When you open LDirStat, the welcome screen gives you a few quick ways to begin:

- **Open Home** scans your home folder
- **Open Root** scans the main filesystem starting at `/`
- **Open Other Directory...** lets you choose any folder

Below that is a list of available filesystems and devices.

- Click a mounted filesystem to scan it
- Click an unmounted device to mount it and then start scanning
- Mounted entries show the device path, mount point, and free space

<!-- Screenshot: welcome screen -->

## Scanning

After you choose a location, LDirStat switches to a scan progress view.

- The progress bar shows that a scan is in progress
- Live counters show how many files and directories have been found so far
- Click **Stop** to cancel the scan

The same progress view is also used when you continue scanning into a mount point later.

If you stop a mount-point continuation, that mount point stays unscanned.

<!-- Screenshot: scan in progress -->

## Main Analysis View

When a scan finishes, the window shows two stacked areas:

- **Directory list** at the top
- **Graph view** at the bottom

The directory list and graph stay in sync, so selecting an item in one updates the other.

<!-- Screenshot: main analysis view -->

## Top Bar and Breadcrumb

The top bar helps you move around the scan without starting over.

- The breadcrumb path shows the directory you are currently looking at
- Click any breadcrumb part to jump back to that folder
- The copy button copies the current directory path
- The clear button jumps back to the scan root
- **Overview** returns to the welcome screen
- **Back** returns from the welcome screen to your current results
- **Rescan** starts the same scan again from the original starting location
- **Graph Type** switches between the available graph views
- The help button opens Help, Report an Issue, and About

<!-- Screenshot: toolbar and breadcrumb -->

## Directory List

The directory list is a column browser. Each column shows the contents of one folder, sorted by size.

- Click a directory to open its contents in a new column
- Files stay in the current column because they have no children
- The footer shows totals for the visible items in that column
- Use the **Filter** box at the top of a column to narrow the list
- The small menu next to the filter gives quick selection actions such as **Select All**, **Clear Filter**, and **Invert Selection**
- The same menu also includes **File Category Statistics...**, which shows a category breakdown for that column's current directory and all subdirectories, even if a text filter is active

Mount points that were skipped during the original scan are marked with **`mnt`** instead of a size.

- Right-click a mount point and choose **Continue Scanning at Mount Point** to scan inside it

<!-- Screenshot: directory list close-up -->

## Graph Views

LDirStat has three graph modes. Use the **Graph Type** button to switch between them.

- **Flame Graph** shows the hierarchy as stacked bars, where wider bars use more space
- **Tree Map - Directory Headers** shows nested rectangles with labeled directory sections
- **Tree Map no headers** shows a denser rectangle view with more detail and fewer labels

You can click items in the graph to select them. Right-clicking a visible graph item opens the same item menu used elsewhere in the app.

<!-- Screenshot: flame graph view -->
<!-- Screenshot: tree map with directory headers -->
<!-- Screenshot: tree map without headers -->

## Right-Click Menu

Right-click an item in the directory list or graph to open the item menu.

Common actions include:

- **Open**
- **Open Terminal**
- **Copy to Clipboard**
- **Move to Trash**
- **Delete Permanently**

For mount points, the menu also includes **Continue Scanning at Mount Point**.

Notes:

- **Copy to Clipboard** copies the selected item path
- The breadcrumb copy button copies the current directory path instead
- **Open Terminal** opens a terminal in the selected directory, or in the parent folder if you selected a file

## Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| Up / Down | Move between items in the current column |
| Left | Move to the parent column |
| Right | Open the selected directory |
| Ctrl+O | Open the selected file or directory |
| Ctrl+T | Open a terminal at the selected location |
| Ctrl+C | Copy the selected item path |
| Delete | Move selected items to trash |
| Ctrl+Delete | Permanently delete selected items |
| Escape | Close the window |

## Tips

- By default, LDirStat scans only the current filesystem. Other mounted filesystems appear as mount points until you continue scanning into them.
- If a mount point moves after being scanned, LDirStat will select that scanned directory for you automatically.
- Rescan always goes back to the original starting location for that result, not to the last folder you clicked.
- You can return to the welcome screen at any time with **Overview** without losing your current scan.
