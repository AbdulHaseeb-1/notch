.pragma library

// The distinct icons of a list of tool steps, in the order they first appear,
// so a row of tool glyphs can show each kind once. Icon names come from the
// C++ side (BuddyProvider::iconForTool); a step without one gets the terminal.
function distinct(steps, max) {
    const seen = []
    if (!steps)
        return seen
    for (let i = 0; i < steps.length && seen.length < max; ++i) {
        const ic = String(steps[i].icon || "terminal")
        if (seen.indexOf(ic) < 0)
            seen.push(ic)
    }
    return seen
}
