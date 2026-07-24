# Changelog

## Unreleased

- Fixed keyboard input freezing the VM by installing and servicing the IRQ1
  interrupt instead of dispatching through an empty IDT entry.
- Enabled keyboard IRQ1 while keeping the timer masked; keyboard bytes are
  drained and acknowledged by the new handler.
