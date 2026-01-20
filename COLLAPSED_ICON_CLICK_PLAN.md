# Collapsed Group Icon Click Feature - Implementation Plan

## Current Behavior - ACTUAL (Verified with Logging)
When clicking on a collapsed group button (e.g., `[wb]`):
- **ANY click** (icon, label, or bracket) triggers the Button's `clicked` handler (line 1580)
- It switches to the last active workspace in that group, or the first workspace if no history
- The EventBox `button_press_event` handler (lines 1545-1555) **is NEVER triggered**

**Root Cause**: The EventBox is inside the Button's content, but GTK is not properly capturing/stopping the event before it reaches the Button. The icon click events propagate through to the parent Button.

## Desired Behavior

### Click on Label/Brackets
- Keep current behavior: focus last used workspace in group
- This happens when clicking on `[`, `]`, or the prefix label

### Click on Icon
Smart window selection based on last active workspace:
1. Identify all windows represented by that icon (can be multiple due to deduplication)
2. Check if any of those windows are in the "last active workspace for the group"
   - If YES: focus that window
3. If NO window in last active workspace:
   - Focus the first window for that application (KISS approach)
   - This window might be in a different workspace in the group

## Code Structure Analysis

### Current Icon Click Handler (lines 1545-1555)
```cpp
eventBox->signal_button_press_event().connect([this, firstWindowAddress](GdkEventButton* event) -> bool {
  if (event->button == 1) {
    spdlog::debug("[WICONS] Collapsed icon clicked, focusing window: {}", firstWindowAddress);
    m_ipc.getSocket1Reply("dispatch focuswindow address:0x" + firstWindowAddress);
    return true;  // Stop propagation to parent button
  }
  return false;
});
```

**Problem**: Only captures `firstWindowAddress` (addresses[0]), ignores other windows

### Available Data (lines 1478-1493)
```cpp
std::map<std::string, std::vector<std::string>> iconToAddresses;
// For each icon, we have ALL window addresses across all workspaces in group
```

### Last Active Workspace Tracking
- Key: `groupPrefix + "@" + monitor`
- Map: `m_lastActivePerGroup` (line 1588)
- Value: workspace name (e.g., ".wb6")

### Window to Workspace Mapping
Need to find which workspace each window address belongs to.
We have `iconToWorkspaceAndTitles[iconName]` which stores pairs of (wsName, windowTitle).
But we need (windowAddress -> wsName) mapping.

## Architecture Question: Single Large Button vs Sibling Buttons

### Current Structure: `[One Large Button containing: label + icons]`
```
Button (collapsed-project class)
├── Box (contentBox)
│   ├── Label ("[")
│   ├── Label (prefix "wb")
│   ├── EventBox → Image (icon1)
│   ├── EventBox → Image (icon2)
│   └── Label ("]")
```

**Problems:**
1. All clicks go to the outer Button - EventBox can't intercept
2. Complex event handling trying to distinguish click targets
3. The entire visual block acts as one clickable unit

### Proposed Structure: `[Sibling Buttons]`
```
Box (contains siblings)
├── Button (label button) → "[wb]"
├── Button (icon1) → Image
├── Button (icon2) → Image
```

**Benefits:**
1. ✅ **Clean event handling**: Each button has its own click handler, no propagation issues
2. ✅ **Simpler code**: No need for EventBox wrappers or event interception
3. ✅ **Independent styling**: Can style label vs icons differently with CSS classes
4. ✅ **Natural GTK behavior**: Buttons handle their own clicks

**Potential Concerns:**
1. **Visual appearance**: Need to ensure buttons look grouped together
   - Solution: Use CSS to remove spacing, add borders to create visual unity
   
2. **CSS selectors**: Does `.collapsed-project` class need to apply to all?
   - Can apply same class to all sibling buttons
   - Or use parent Box class and child selectors
   
3. **Hover/focus states**: Each button might highlight individually
   - This might actually be GOOD - shows which element you're about to click
   - Can override in CSS if unified hover is needed

### CSS Implications

Current (assumed):
```css
.collapsed-project {
  /* Styles the entire collapsed group as one unit */
}
```

New approach options:

**Option A - Same class on all:**
```css
.collapsed-project-label { /* Label button */ }
.collapsed-project-icon  { /* Icon buttons */ }
```

**Option B - Parent container:**
```css
.collapsed-project-group { /* Box containing siblings */ }
.collapsed-project-group > button { /* All buttons */ }
```

### Recommendation

**Use sibling buttons** - it's the simpler, more maintainable approach:

1. **Cleaner architecture**: Each UI element does one thing
2. **No event fighting**: GTK works as designed
3. **Easier to reason about**: Click label → workspace, click icon → window
4. **Future-proof**: Adding new elements is straightforward

The only real "benefit" of the large button was visual grouping, which CSS can handle just as well.

### Step 0: Fix Event Handling - Make EventBox Capture Clicks
The EventBox needs to properly intercept events before they reach the Button:

```cpp
// Enable event handling on EventBox
eventBox->add_events(Gdk::BUTTON_PRESS_MASK);
eventBox->set_visible_window(false);  // Keep transparent
```

Also need to ensure the handler runs in capture phase and actually stops propagation:
```cpp
eventBox->signal_button_press_event().connect([...](GdkEventButton* event) -> bool {
  // ... handle click ...
  return true;  // This should stop propagation but currently doesn't work
}, false);  // false = run in capture phase, before default handler
```

**Alternative Approach if EventBox doesn't work:**
Instead of trying to intercept events in EventBox, make the icons actual Buttons:
```cpp
auto* iconBtn = Gtk::manage(new Gtk::Button());
iconBtn->set_relief(Gtk::RELIEF_NONE);
iconBtn->add(*icon);
// iconBtn.signal_clicked() will fire INSTEAD of parent button
```

### Step 1: Build Window Address to Workspace Mapping
When collecting icons (lines 1480-1495), also build:
```cpp
std::map<std::string, std::string> addressToWorkspace;
// Maps window address -> workspace name
```

### Step 2: Enhance Icon Click Handler
Replace simple `firstWindowAddress` capture with:
```cpp
// Capture ALL needed data
std::string iconName = ...;  // The icon identifier
std::vector<std::string> allAddresses = iconToAddresses[iconName];
std::map<std::string, std::string> addrToWs = addressToWorkspace;
std::string groupPrefixCopy = groupPrefix;
std::string monitorCopy = getBarOutput();

eventBox->signal_button_press_event().connect(
  [this, iconName, allAddresses, addrToWs, groupPrefixCopy, monitorCopy]
  (GdkEventButton* event) -> bool {
    if (event->button == 1) {
      std::string targetAddress = selectBestWindowForIcon(
        allAddresses, addrToWs, groupPrefixCopy, monitorCopy
      );
      spdlog::debug("[WICONS] Collapsed icon '{}' clicked, focusing: {}", 
                    iconName, targetAddress);
      m_ipc.getSocket1Reply("dispatch focuswindow address:0x" + targetAddress);
      return true;
    }
    return false;
  });
```

### Step 3: Implement Selection Algorithm
```cpp
std::string Workspaces::selectBestWindowForIcon(
  const std::vector<std::string>& addresses,
  const std::map<std::string, std::string>& addressToWorkspace,
  const std::string& groupPrefix,
  const std::string& monitor
) {
  // Build key for last active lookup
  std::string key = groupPrefix + "@" + monitor;
  
  // Try to find last active workspace
  auto it = m_lastActivePerGroup.find(key);
  if (it != m_lastActivePerGroup.end()) {
    std::string lastActiveWs = it->second;
    
    // Look for a window in that workspace
    for (const auto& addr : addresses) {
      auto wsIt = addressToWorkspace.find(addr);
      if (wsIt != addressToWorkspace.end() && wsIt->second == lastActiveWs) {
        spdlog::debug("[WICONS] Found window in last active workspace '{}'", lastActiveWs);
        return addr;
      }
    }
  }
  
  // Fallback: return first window
  spdlog::debug("[WICONS] No window in last active workspace, using first");
  return addresses[0];
}
```

### Step 4: Add Method Declaration
In `workspaces.hpp`, add to Workspaces class:
```cpp
private:
  std::string selectBestWindowForIcon(
    const std::vector<std::string>& addresses,
    const std::map<std::string, std::string>& addressToWorkspace,
    const std::string& groupPrefix,
    const std::string& monitor
  );
```

## Edge Cases

1. **Empty addresses vector**: Should never happen (icon only added if addresses exist)
2. **addressToWorkspace missing entry**: Shouldn't happen but fallback to first window
3. **Last active workspace deleted**: Fallback to first window (already handled)
4. **Multiple windows in last active workspace**: Pick first match (KISS)

## Testing Checklist

- [ ] Click on icon in collapsed group with single window
- [ ] Click on icon representing multiple windows in same workspace
- [ ] Click on icon representing windows across multiple workspaces
- [ ] Click on icon when last active workspace has the window
- [ ] Click on icon when last active workspace doesn't have the window
- [ ] Click on `[`, `]`, or prefix label -> should focus workspace (existing behavior)
- [ ] Verify click propagation is stopped (icon click shouldn't trigger button click)

## Files to Modify

1. `src/modules/hyprland/workspaces.cpp`:
   - Modify icon click handler (~line 1545)
   - Build addressToWorkspace map (~line 1490)
   - Add selectBestWindowForIcon() method (new)

2. `include/modules/hyprland/workspaces.hpp`:
   - Add selectBestWindowForIcon() declaration

## Estimated Lines Changed

- ~30 lines added (new method + data collection)
- ~15 lines modified (icon click handler)
- Total: ~45 lines
