"""Scripted mouse and keyboard for tutorial takes (monitor 1, real pixels).

    python tools/tutorial/act.py "go:858,386" "type:blue_preytor" "key:enter" "wait:2" "arrow:down,2" "hold:space,0.12"

Actions:
  wait:S            sleep S seconds
  key:K             tap a key (pyautogui name)
  hold:K,S          hold a key S seconds (the game polls once per frame: taps shorter than a frame get lost)
  arrow:D[,N]       arrow key D (up/down/left/right) N times, sent as an extended key
  ahold:D,S         hold arrow D for S seconds
  type:TEXT         type text
  hot:A+B           hotkey
  move:X,Y          glide the cursor (0.5 s) to X,Y
  go:X,Y            glide and left click (what a viewer sees in the takes)
  click:X,Y         click without gliding
  dclick:X,Y        glide and double click
  rclick:X,Y        glide and right click
  mhold:BTN,S       hold mouse button BTN (left/right/middle) S seconds
  drag:DX,DY,S      relative mouse motion over S seconds (turns the game camera)
  bdrag:BTN,DX,DY,S the same with mouse button BTN held (photo mode LOOK is the right button)
  wheel:N[,S]       N wheel notches (negative = toward the user), S seconds apart

pyautogui sends arrow keys without KEYEVENTF_EXTENDEDKEY, and GLFW then reads them as the numeric keypad, so
arrows go through SendInput here.
"""
import ctypes
import sys
import time
from ctypes import wintypes

import pyautogui

pyautogui.FAILSAFE = False
pyautogui.PAUSE = 0.05
GLIDE = 0.5

_SCAN = {'up': 0x48, 'down': 0x50, 'left': 0x4B, 'right': 0x4D}
_VK = {'up': 0x26, 'down': 0x28, 'left': 0x25, 'right': 0x27}


class _KI(ctypes.Structure):
    _fields_ = [('wVk', wintypes.WORD), ('wScan', wintypes.WORD), ('dwFlags', wintypes.DWORD),
                ('time', wintypes.DWORD), ('dwExtraInfo', ctypes.c_void_p)]


class _MI(ctypes.Structure):
    _fields_ = [('dx', wintypes.LONG), ('dy', wintypes.LONG), ('mouseData', wintypes.DWORD),
                ('dwFlags', wintypes.DWORD), ('time', wintypes.DWORD), ('dwExtraInfo', ctypes.c_void_p)]


class _U(ctypes.Union):
    _fields_ = [('ki', _KI), ('mi', _MI), ('pad', ctypes.c_byte * 32)]


class _INPUT(ctypes.Structure):
    _anonymous_ = ('u',)
    _fields_ = [('type', wintypes.DWORD), ('u', _U)]


def _send(inp):
    ctypes.windll.user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(_INPUT))


def arrow(name, secs=0.12):
    for up in (False, True):
        i = _INPUT()
        i.type = 1
        i.ki = _KI(_VK[name], _SCAN[name], 0x1 | (0x2 if up else 0), 0, None)
        _send(i)
        if not up:
            time.sleep(secs)


def rel_move(dx, dy):
    i = _INPUT()
    i.type = 0
    i.mi = _MI(dx, dy, 0, 0x1, 0, None)   # MOUSEEVENTF_MOVE (relative)
    _send(i)


def drag(dx, dy, secs):
    steps = max(1, int(secs * 60))
    ax = ay = 0
    for s in range(1, steps + 1):
        tx, ty = round(dx * s / steps), round(dy * s / steps)
        rel_move(tx - ax, ty - ay)
        ax, ay = tx, ty
        time.sleep(secs / steps)


def xy(v):
    x, y = v.split(',')[:2]
    return int(x), int(y)


def run(actions):
    for a in actions:
        k, _, v = a.partition(':')
        if k == 'wait':
            time.sleep(float(v))
        elif k == 'key':
            pyautogui.press(v)
        elif k == 'hold':
            key, secs = v.split(',')
            pyautogui.keyDown(key)
            time.sleep(float(secs))
            pyautogui.keyUp(key)
        elif k == 'arrow':
            name, _, n = v.partition(',')
            for _ in range(int(n or 1)):
                arrow(name)
                time.sleep(0.35)
        elif k == 'ahold':
            name, secs = v.split(',')
            arrow(name, float(secs))
        elif k == 'type':
            pyautogui.write(v, interval=0.06)
        elif k == 'hot':
            pyautogui.hotkey(*v.split('+'))
        elif k == 'move':
            pyautogui.moveTo(*xy(v), duration=GLIDE)
        elif k == 'go':
            pyautogui.moveTo(*xy(v), duration=GLIDE)
            pyautogui.click()
        elif k == 'click':
            pyautogui.click(*xy(v))
        elif k == 'dclick':
            pyautogui.moveTo(*xy(v), duration=GLIDE)
            pyautogui.doubleClick()
        elif k == 'rclick':
            pyautogui.moveTo(*xy(v), duration=GLIDE)
            pyautogui.rightClick()
        elif k == 'mhold':
            btn, secs = v.split(',')
            pyautogui.mouseDown(button=btn)
            time.sleep(float(secs))
            pyautogui.mouseUp(button=btn)
        elif k == 'drag':
            dx, dy, secs = v.split(',')
            drag(int(dx), int(dy), float(secs))
        elif k == 'bdrag':
            btn, dx, dy, secs = v.split(',')
            pyautogui.mouseDown(button=btn)
            time.sleep(0.1)
            drag(int(dx), int(dy), float(secs))
            time.sleep(0.1)
            pyautogui.mouseUp(button=btn)
        elif k == 'wheel':
            n, _, secs = v.partition(',')
            for _ in range(abs(int(n))):
                pyautogui.scroll(120 if int(n) > 0 else -120)
                time.sleep(float(secs or 0.08))
        else:
            raise SystemExit('unknown action %r' % a)


if __name__ == '__main__':
    run(sys.argv[1:])
