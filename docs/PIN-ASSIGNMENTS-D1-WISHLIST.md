# D1 Mini32 Pin Assignment Wishlist (2026-05-18)

## Final mapping
- Buttons RGBW: GPIO16, GPIO17, GPIO21, GPIO22
- Button LEDs RGBW: GPIO23, GPIO19, GPIO18, GPIO26
- LED strip data: GPIO33
- Piezo: GPIO27

## Note on piezo alternatives
- GPIO34 and GPIO35 are input-only on ESP32, so they cannot drive piezo output.

## Validation checklist
1. Confirm all selected pins exist on your exact D1 Mini32 board variant.
2. Verify no boot strap conflicts on chosen pins.
3. Run cold boot and reset cycles after wiring.
4. Verify LED strip startup stability and no boot loops.
5. Verify piezo tone output with a 1 kHz test signal.
