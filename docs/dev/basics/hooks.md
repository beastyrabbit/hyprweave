# Function hooks

A function hook replaces the start of a Hyprland function with a jump to plugin code. The plugin can run code before or after the original, change its arguments or result, or skip it. Hooks are the most fragile part of a plugin: any Hyprland release may rename, inline, split or remove the hooked function. Use the API or the event bus first, and hook only what they cannot do.

Hooks work only on x86_64. On other architectures Hyprland ignores the hook request.

## Recipe

```cpp
inline CFunctionHook* g_pUpdateVisibilityHook = nullptr;

// member function: `this` comes first
typedef void (*origUpdateVisibility)(Desktop::View::CGroup*);

static void hkUpdateVisibility(Desktop::View::CGroup* thisptr) {
    // before
    (*(origUpdateVisibility)g_pUpdateVisibilityHook->m_original)(thisptr);
    // after
}

// in PLUGIN_INIT
for (auto& fn : HyprlandAPI::findFunctionsByName(PHANDLE, "updateWindowVisibility")) {
    if (fn.demangled != "Desktop::View::CGroup::updateWindowVisibility()")
        continue;
    g_pUpdateVisibilityHook = HyprlandAPI::createFunctionHook(PHANDLE, fn.address, (void*)&hkUpdateVisibility);
    break;
}

if (!g_pUpdateVisibilityHook || !g_pUpdateVisibilityHook->hook()) {
    HyprlandAPI::addNotification(PHANDLE, "[hyprweave] Could not hook CGroup::updateWindowVisibility", CHyprColor{1.0, 0.2, 0.2, 1.0}, 5000);
    throw std::runtime_error("[hyprweave] hook failed");
}
```

Rules:

- The original is `m_original` in 0.57. The wiki still says `m_pOriginal`, which no longer compiles.
- Member functions take the object pointer as the first argument. Arguments passed by reference in C++ stay references in the typedef (`const CBox&`, `const STargetBox&`).
- Match the full demangled signature, not just the name. `findFunctionsByName` returns every exported symbol whose **mangled** name contains the string. `"updatePos"` matches both `Layout::CWindowTarget::updatePos(unsigned char)` and `Layout::CWindowGroupTarget::updatePos(unsigned char)`, and `"setCurrent"` matches both `CGroup::setCurrent(unsigned long)` and `CGroup::setCurrent(Hyprutils::Memory::CSharedPointer<Desktop::View::CWindow>)`. Pick the one you mean by `fn.demangled`. A wrong pick crashes the compositor.
- If a required hook is missing or `hook()` returns false, refuse to load. Half-installed hooks leave groups in a broken state.
- Do the lookups once in `PLUGIN_INIT`. The first lookup runs `nm -D` over the Hyprland binary, so it is slow, and the addresses do not change at runtime. It needs `nm` (binutils) in Hyprland's `PATH`; on bunux it is `/usr/bin/nm`.
- `unhook()` and `hook()` can toggle a hook at runtime. Hyprland removes all of a plugin's hooks on unload, after `PLUGIN_EXIT`.

## Check that a hook will fire

A hook only catches calls that go through the function's entry point. If the compiler inlined the function into a caller, that call site bypasses the hook. Exported (`T`) symbols are callable, but that does not prove every caller uses them. Check the installed binary:

```sh
# is the symbol exported?
nm -DC /usr/bin/Hyprland | grep 'CGroup::updateWindowVisibility'

# how many direct calls go to it? compare with the call sites in the source
objdump -d --no-show-raw-insn -C /usr/bin/Hyprland \
    | grep -E '(call|jmp).*<Desktop::View::CGroup::updateWindowVisibility\(\)>$' | wc -l
```

Results for 0.57.0 on bunux (2026-10-30):

- `CGroup::updateWindowVisibility()` has 7 direct calls. That matches the 7 calls in `Group.cpp` (`init`, `add`, `replaceMember`, `remove`, `setCurrent(size_t)`, `swapWithNext`, `swapWithLast`), so no caller inlined it.
- `CWindowGroupTarget::setPositionGlobal` calls `ITarget::setPositionGlobal` and then tail-jumps to `CWindowGroupTarget::updatePos`, so a hook on `updatePos` sees every group position update.

Virtual functions called through the vtable also go through the entry point, so hooking the implementation catches them.

Repeat this check for every hook target after a Hyprland update.

## Hook versus other options

The plugin guidelines say to prefer the API and event hooks and to treat function hooks as a last resort. Spelled out, that gives this order:

1. The plugin API in `PluginAPI.hpp`, which stays stable within an API version.
2. Event bus listeners.
3. Public methods of core objects, called directly. They are not guaranteed stable, but methods that look fundamental rarely change.
4. Private members through `#define private public` (see [plugin-api.md](plugin-api.md#private-members)).
5. Function hooks.

hyprweave cannot avoid level 5, because the core decides group visibility and geometry inside `CGroup` and `CWindowGroupTarget` and announces nothing on the event bus. Keep the hook set small and document each hook's target and reason next to the code.
