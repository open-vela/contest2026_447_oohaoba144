"""Check the LCD board touch IRQ without changing the upstream defconfig."""
import re
import sys
from pathlib import Path


def check(path):
    text = Path(path).read_text(encoding="utf-8")
    def value(key):
        found = re.findall(r"^" + re.escape(key) + r"=(.*)$", text, re.M)
        if len(found) != 1:
            raise ValueError(f"{key}: expected one active definition")
        return found[0].strip()
    board = value("CONFIG_ARCH_BOARD_CUSTOM_DIR").strip('"')
    if board.split("/")[-1] != "sf32lb52_devkit_lcd":
        raise ValueError("This check is only for SF32LB52-DevKit-LCD")
    if value("CONFIG_INPUT_FT6146") != "y":
        raise ValueError("FT6146 must be enabled")
    irq = value("CONFIG_TOUCH_IRQ_PIN")
    if irq != "31":
        raise ValueError(f"LCD CTP_INT is PA31; found PA{irq} (PA41 belongs to ULP)")


if __name__ == "__main__":
    try:
        check(sys.argv[1])
    except (OSError, ValueError, IndexError) as error:
        print(f"FAIL: {error}")
        sys.exit(1)
    print("PASS: SF32LB52-DevKit-LCD FT6146 IRQ=PA31")