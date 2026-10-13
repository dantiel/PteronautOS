// Keyboard-cycle for the plain (non-MUI) <select> elements used by the
// PteronautOS panels. Native dropdowns otherwise force type-ahead selection
// (typing "1", "2", "3") on some platforms — this remaps Tab/Shift+Tab to
// cycle the selected option so the value can be changed without entering
// numbers or reaching for the mouse.
//
// Scope: only bare <select> tags (tagName === 'SELECT'). The ELRS pages wrap
// their selects in `.mui-select` (inner select has tabIndex=-1 and never
// receives focus), so they are unaffected.

export function initKeyboardSelect() {
    const onKeyDown = (event) => {
        const el = event.target
        if (!el || el.tagName !== 'SELECT') return
        // Belt-and-suspenders: leave the MUI custom-select wrapper alone.
        if (el.closest('.mui-select')) return
        if (event.key !== 'Tab') return

        event.preventDefault()
        cycleOption(el, event.shiftKey ? -1 : 1)
    }

    document.addEventListener('keydown', onKeyDown)
    return () => document.removeEventListener('keydown', onKeyDown)
}

function cycleOption(select, delta) {
    const options = Array.from(select.options).filter((o) => !o.disabled && !o.hidden)
    if (options.length === 0) return

    const currentIndex = select.selectedIndex
    let pos = options.findIndex((o) => o.index === currentIndex)
    if (pos === -1) pos = 0

    const nextPos = (pos + delta + options.length) % options.length
    const next = options[nextPos]
    if (next.index === currentIndex) return

    select.selectedIndex = next.index
    select.dispatchEvent(new Event('change', { bubbles: true }))
}
