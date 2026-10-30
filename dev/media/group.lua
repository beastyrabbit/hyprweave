-- stream on top, chat below, locked together
hl.plugin.hyprweave.create({
    direction = "column",
    lock = "full",
    children = {
        { window = "class:^demo-stream$", size = 75 },
        { window = "class:^demo-chat$", size = 25 },
    },
})
