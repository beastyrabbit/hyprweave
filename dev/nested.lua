-- Config for the nested Hyprland used to develop hyprweave (started by `dev/nest start`).
-- No autostart, and ALT instead of SUPER so binds do not clash with the outer session.

hl.monitor({
    output   = "",
    mode     = "preferred",
    position = "auto",
    scale    = 1,
})

hl.config({
    input = {
        kb_layout    = "de",
        follow_mouse = 1,
    },
    general = {
        gaps_in     = 5,
        gaps_out    = 5,
        border_size = 2,
        layout      = "dwindle",
    },
    decoration = {
        rounding = 12,
    },
    misc = {
        disable_hyprland_logo    = true,
        disable_splash_rendering = true,
        force_default_wallpaper  = 0,
    },
})

local mod = "ALT"

hl.bind(mod .. " + Return", hl.dsp.exec_cmd("kitty"))
hl.bind(mod .. " + Q", hl.dsp.window.close())
hl.bind(mod .. " + SHIFT + E", hl.dsp.exit())

hl.bind(mod .. " + left",  hl.dsp.focus({ direction = "left" }))
hl.bind(mod .. " + right", hl.dsp.focus({ direction = "right" }))
hl.bind(mod .. " + up",    hl.dsp.focus({ direction = "up" }))
hl.bind(mod .. " + down",  hl.dsp.focus({ direction = "down" }))

-- Groups
hl.bind(mod .. " + W", hl.dsp.group.toggle())
hl.bind(mod .. " + Tab", hl.dsp.group.next())
hl.bind(mod .. " + SHIFT + Tab", hl.dsp.group.prev())
