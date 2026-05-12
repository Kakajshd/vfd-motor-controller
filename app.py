import serial
import serial.tools.list_ports
import tkinter as tk
from tkinter import messagebox
import time
import sys
import os


def resource_path(relative):
    """Resolve path for both script mode and PyInstaller exe."""
    if hasattr(sys, '_MEIPASS'):
        return os.path.join(sys._MEIPASS, relative)
    return os.path.join(os.path.dirname(os.path.abspath(__file__)), relative)

# ================= CONFIG =================
BAUDRATE = 115200
SCAN_INTERVAL_MS = 1500
READ_INTERVAL_MS = 100
VALID_VIDS = (0x303A, 0x10C4, 0x1A86)
PROBE_TIMEOUT_SEC = 1.5
PROBE_MARKERS = ("CURRENT SETTINGS", "UD (MIN/MAX)", "UF (MIN/MAX)", "BOOST TIME")
PROBE_MIN_MARKERS = 2
PROBE_RETRY_COUNT = 3

MAX_LOG_LINES         = 300
TIMEOUT_APPLYING_MS   = 5000
TIMEOUT_RESTARTING_MS = 10000

STATE_DISCONNECTED = "DISCONNECTED"
STATE_CONNECTING   = "CONNECTING"
STATE_READY        = "READY"
STATE_APPLYING     = "APPLYING"
STATE_RESTARTING   = "RESTARTING"
STATE_ERROR        = "ERROR"

STATE_COLORS = {
    STATE_DISCONNECTED: "#ff6666",
    STATE_CONNECTING:   "#ffaa00",
    STATE_READY:        "#00cc66",
    STATE_APPLYING:     "#ffff00",
    STATE_RESTARTING:   "#ff9900",
    STATE_ERROR:        "#ff4444",
}

STATE_MESSAGES = {
    STATE_DISCONNECTED: "Disconnected",
    STATE_CONNECTING:   "Connecting...",
    STATE_READY:        "Ready",
    STATE_APPLYING:     "Applying...",
    STATE_RESTARTING:   "Restarting...",
    STATE_ERROR:        "Error - check connection",
}

app_state          = STATE_DISCONNECTED
pending_timeout_id = None

# Deferred UI widget references (assigned after widget creation)
status_var   = None
status_label = None
btn_apply    = None
btn_reset    = None

ser = None

# ================= SERIAL =================
def open_serial(port):
    try:
        return serial.Serial(port, BAUDRATE, timeout=0.2)
    except:
        return None

def close_serial():
    global ser
    if ser:
        try: ser.close()
        except: pass
    ser = None
    set_app_state(STATE_DISCONNECTED)

def probe_target_device(port_info):
    s = open_serial(port_info.device)
    if not s:
        return None

    try:
        # ESP32 USB CDC often resets when COM is opened; give it a short settle time.
        time.sleep(0.35)
        s.reset_input_buffer()
        s.reset_output_buffer()

        joined = ""
        for _ in range(PROBE_RETRY_COUNT):
            s.write(b"INFO\n")

            deadline = time.monotonic() + PROBE_TIMEOUT_SEC
            lines = []
            while time.monotonic() < deadline:
                raw = s.readline()
                if not raw:
                    continue

                line = raw.decode(errors="ignore").strip()
                if not line:
                    continue

                lines.append(line)
                joined = "\n".join(lines).upper()

                # Accept quickly if runtime logs are already visible.
                if "[RUN]" in joined or "[PING]" in joined:
                    return s

                marker_count = sum(1 for marker in PROBE_MARKERS if marker in joined)
                if marker_count >= PROBE_MIN_MARKERS:
                    return s
    except:
        pass

    try:
        s.close()
    except:
        pass
    return None

# ================= STATE HELPERS =================
def set_app_state(new_state, message=None):
    global app_state
    app_state = new_state
    try:
        msg = message if message is not None else STATE_MESSAGES.get(new_state, "")
        status_var.set(msg)
        status_label.config(fg=STATE_COLORS.get(new_state, "#ffcc00"))
    except (NameError, AttributeError, tk.TclError):
        pass
    update_button_states()


def update_button_states():
    try:
        is_valid = validate_entries()
        active = "normal" if app_state == STATE_READY and is_valid else "disabled"
        rst    = "normal" if app_state in (STATE_READY, STATE_APPLYING) else "disabled"
        btn_apply.config(state=active)
        btn_reset.config(state=rst)
    except (NameError, AttributeError, tk.TclError):
        pass


def cancel_pending_timeout():
    global pending_timeout_id
    if pending_timeout_id is not None:
        try:
            root.after_cancel(pending_timeout_id)
        except Exception:
            pass
        pending_timeout_id = None


def schedule_state_timeout(expected_state, timeout_ms):
    cancel_pending_timeout()
    global pending_timeout_id
    pending_timeout_id = root.after(timeout_ms, lambda: on_state_timeout(expected_state))


def on_state_timeout(expected_state):
    global pending_timeout_id
    pending_timeout_id = None
    if app_state == expected_state:
        append_log(f"[SYS] Timeout: no response (state={expected_state})")
        set_app_state(STATE_ERROR)


# ================= LOG BUFFER =================
def append_log(text):
    log.insert(tk.END, text + "\n")
    line_count = int(log.index(tk.END).split(".")[0]) - 1
    if line_count > MAX_LOG_LINES:
        log.delete("1.0", f"{line_count - MAX_LOG_LINES + 1}.0")
    log.see(tk.END)


# ================= VALIDATION =================
def _safe_float(widget):
    try:
        return float(widget.get()), True
    except (ValueError, tk.TclError):
        return 0.0, False


def set_entry_color(widget, is_valid):
    try:
        widget.config(
            bg="#111111" if is_valid else "#330000",
            fg="white"   if is_valid else "#ff8888"
        )
    except tk.TclError:
        pass


def validate_entries():
    try:
        udmin, ok1  = _safe_float(e_udmin)
        udmax, ok2  = _safe_float(e_udmax)
        ufmin, ok3  = _safe_float(e_ufmin)
        ufmax, ok4  = _safe_float(e_ufmax)
        l1,    ok5  = _safe_float(e_boot)
        h1,    ok6  = _safe_float(e_boosttime)
        l2,    ok7  = _safe_float(e_boost_l2)
        l3,    ok8  = _safe_float(e_boost_l3)
        h2,    ok9  = _safe_float(e_boost_h2)
        h3,    ok10 = _safe_float(e_boost_h3)
        e2,    ok11 = _safe_float(e_boost_e2)
        e3,    ok12 = _safe_float(e_boost_e3)
        dc,    ok13 = _safe_float(e_boost_decay)
    except NameError:
        return True

    v_udmin = ok1
    v_udmax = ok2 and udmax > udmin
    v_ufmin = ok3
    v_ufmax = ok4 and ufmax > ufmin
    v_l1    = ok5 and l1 >= 0
    v_h1    = ok6 and h1 >= 0
    v_l2    = ok7 and l2 >= l1
    v_l3    = ok8 and l3 >= l2
    v_h2    = ok9 and h2 >= 0
    v_h3    = ok10 and h3 >= 0
    v_e2    = ok11
    v_e3    = ok12 and e3 > e2
    v_dc    = ok13 and dc >= 0

    set_entry_color(e_udmin,       v_udmin)
    set_entry_color(e_udmax,       v_udmax)
    set_entry_color(e_ufmin,       v_ufmin)
    set_entry_color(e_ufmax,       v_ufmax)
    set_entry_color(e_boot,        v_l1)
    set_entry_color(e_boosttime,   v_h1)
    set_entry_color(e_boost_l2,    v_l2)
    set_entry_color(e_boost_l3,    v_l3)
    set_entry_color(e_boost_h2,    v_h2)
    set_entry_color(e_boost_h3,    v_h3)
    set_entry_color(e_boost_e2,    v_e2)
    set_entry_color(e_boost_e3,    v_e3)
    set_entry_color(e_boost_decay, v_dc)

    return all([v_udmin, v_udmax, v_ufmin, v_ufmax, v_l1, v_h1,
                v_l2, v_l3, v_h2, v_h3, v_e2, v_e3, v_dc])


# ================= AUTO CONNECT =================
def auto_scan():
    global ser

    ports = serial.tools.list_ports.comports()
    port_names = [p.device for p in ports]

    # detect mất cổng
    if ser:
        try:
            if ser.port not in port_names:
                close_serial()
                port_var.set("---")
        except:
            close_serial()
            port_var.set("---")

    # reconnect
    if ser is None:
        candidates = [p for p in ports if p.vid in VALID_VIDS]
        if not candidates:
            # Fallback: some USB-UART adapters report missing/odd VID values.
            candidates = ports

        if candidates and app_state == STATE_DISCONNECTED:
            set_app_state(STATE_CONNECTING)

        connected = False
        for p in candidates:
            s = probe_target_device(p)
            if s:
                ser = s
                port_var.set(p.device)
                append_log(f"[SYS] Connected {p.device} ({p.description})")
                set_app_state(STATE_READY)
                root.after(200, request_info)
                connected = True
                break

        if not connected and app_state == STATE_CONNECTING:
            set_app_state(STATE_DISCONNECTED)

    root.after(SCAN_INTERVAL_MS, auto_scan)

# ================= COMMAND =================
def request_info():
    if ser and ser.is_open:
        ser.write(b"INFO\n")

def send_param():
    if not ser or not ser.is_open:
        messagebox.showerror("Error", "Not connected")
        return

    try:
        udmin = float(e_udmin.get())
        udmax = float(e_udmax.get())
        ufmin = float(e_ufmin.get())
        ufmax = float(e_ufmax.get())
        boost_l1 = float(e_boot.get())
        boost_h1 = float(e_boosttime.get())
        boost_l2 = float(e_boost_l2.get())
        boost_l3 = float(e_boost_l3.get())
        boost_h2 = float(e_boost_h2.get())
        boost_h3 = float(e_boost_h3.get())
        boost_e2 = float(e_boost_e2.get())
        boost_e3 = float(e_boost_e3.get())
        boost_decay = float(e_boost_decay.get())
    except:
        messagebox.showerror("Error", "Invalid input")
        return

    if udmax <= udmin or ufmax <= ufmin:
        messagebox.showerror("Error", "Invalid range")
        return

    if boost_l1 < 0 or boost_l2 < boost_l1 or boost_l3 < boost_l2:
        messagebox.showerror("Error", "Boost levels must be increasing")
        return

    if boost_h1 < 0 or boost_h2 < 0 or boost_h3 < 0:
        messagebox.showerror("Error", "Boost hold must be >= 0")
        return

    if boost_e3 <= boost_e2:
        messagebox.showerror("Error", "ESC L3 must be greater than ESC L2")
        return

    if boost_decay < 0:
        messagebox.showerror("Error", "Invalid range")
        return

    set_app_state(STATE_APPLYING)
    schedule_state_timeout(STATE_APPLYING, TIMEOUT_APPLYING_MS)

    payload = ",".join(f"{v:.3f}" for v in [udmin, udmax, ufmin, ufmax, boost_l1, boost_h1])
    ser.write((f"TXN={payload}\n").encode())

    extra_commands = [
        ("BOOSTL2", boost_l2),
        ("BOOSTL3", boost_l3),
        ("BOOSTH2", boost_h2),
        ("BOOSTH3", boost_h3),
        ("BOOSTE2", boost_e2),
        ("BOOSTE3", boost_e3),
        ("BOOSTDECAY", boost_decay),
    ]
    for key, val in extra_commands:
        ser.write((f"{key}={val:.3f}\n").encode())
        time.sleep(0.03)

def reset_esp():
    if not ser or not ser.is_open:
        messagebox.showerror("Error", "Not connected")
        return

    if not messagebox.askyesno("Confirm", "Reset ESP32 ngay bây giờ?"):
        return

    try:
        ser.write(b"RESET\n")
        append_log("[SYS] Sent RESET command")
        set_app_state(STATE_RESTARTING)
        schedule_state_timeout(STATE_RESTARTING, TIMEOUT_RESTARTING_MS)
    except:
        messagebox.showerror("Error", "Failed to send RESET")

# ================= PARSE =================
def parse_line(line):

    # ===== LOAD SETTINGS =====
    if "UD (Min/Max)" in line:
        try:
            vals = line.split(":")[1].split("/")
            e_udmin.delete(0, tk.END)
            e_udmin.insert(0, vals[0].strip())
            e_udmax.delete(0, tk.END)
            e_udmax.insert(0, vals[1].strip())
        except: pass

    elif "UF (Min/Max)" in line:
        try:
            vals = line.split(":")[1].split("/")
            e_ufmin.delete(0, tk.END)
            e_ufmin.insert(0, vals[0].strip())
            e_ufmax.delete(0, tk.END)
            e_ufmax.insert(0, vals[1].strip())
        except: pass

    elif "Boot Factor" in line:
        try:
            val = line.split(":")[1].strip()
            e_boot.delete(0, tk.END)
            e_boot.insert(0, val)
        except: pass

    elif "Boost Time" in line:
        try:
            val = line.split(":")[1].strip().split(" ")[0]  # Bỏ "s" ở cuối
            e_boosttime.delete(0, tk.END)
            e_boosttime.insert(0, val)
        except: pass

    elif "Boost L1/L2/L3" in line:
        try:
            vals = [v.strip() for v in line.split(":", 1)[1].split("/")]
            if len(vals) >= 3:
                e_boot.delete(0, tk.END)
                e_boot.insert(0, vals[0])
                e_boost_l2.delete(0, tk.END)
                e_boost_l2.insert(0, vals[1])
                e_boost_l3.delete(0, tk.END)
                e_boost_l3.insert(0, vals[2])
        except: pass

    elif "Boost Hold L1/L2/L3" in line:
        try:
            vals = [v.strip().split(" ")[0] for v in line.split(":", 1)[1].split("/")]
            if len(vals) >= 3:
                e_boosttime.delete(0, tk.END)
                e_boosttime.insert(0, vals[0])
                e_boost_h2.delete(0, tk.END)
                e_boost_h2.insert(0, vals[1])
                e_boost_h3.delete(0, tk.END)
                e_boost_h3.insert(0, vals[2])
        except: pass

    elif "Boost Esc2/Esc3" in line:
        try:
            vals = [v.strip().split(" ")[0] for v in line.split(":", 1)[1].split("/")]
            if len(vals) >= 2:
                e_boost_e2.delete(0, tk.END)
                e_boost_e2.insert(0, vals[0])
                e_boost_e3.delete(0, tk.END)
                e_boost_e3.insert(0, vals[1])
        except: pass

    elif "Boost Decay" in line:
        try:
            val = line.split(":", 1)[1].strip().split(" ")[0]
            e_boost_decay.delete(0, tk.END)
            e_boost_decay.insert(0, val)
            root.after(0, update_button_states)
        except: pass

    # ===== PARAM APPLY OK =====
    elif "Đã cập nhật" in line or "[TXN] OK" in line:
        cancel_pending_timeout()
        set_app_state(STATE_READY, "Applied OK")
        root.after(2000, lambda: set_app_state(STATE_READY) if app_state == STATE_READY else None)

    # ===== RUN =====
    elif "[RUN]" in line:
        try:
            parts = [p.strip() for p in line.split("|")]
            dist = parts[0].split("D:", 1)[1].strip()
            freq = parts[1].split("F:", 1)[1].replace("Hz", "").strip()

            boost_level = "--"
            boost_hold = "--"
            for part in parts[2:]:
                if "Boost:" in part:
                    boost_level = part.split("Boost:", 1)[1].strip()
                elif "Hold:" in part:
                    boost_hold = part.split("Hold:", 1)[1].strip()

            dist_var.set(dist + " cm")
            target_var.set(freq + " Hz")
            boost_level_var.set(boost_level)
            boost_hold_var.set(boost_hold)
            mode_var.set("AUTO")
            if app_state in (STATE_RESTARTING, STATE_ERROR):
                cancel_pending_timeout()
                set_app_state(STATE_READY)
        except:
            pass

    # ===== PING =====
    elif "[PING]" in line:
        mode_var.set("TEST")
        if app_state in (STATE_RESTARTING, STATE_ERROR):
            cancel_pending_timeout()
            set_app_state(STATE_READY)

    # ===== WIFI / SYSTEM =====
    elif "[WIFI]" in line:
        try:
            if "Connected, IP:" in line:
                ip = line.split("IP:", 1)[1].strip()
                wifi_var.set(f"WebServer \u25ba http://{ip}")
            elif "Disconnected" in line or "failed" in line.lower():
                wifi_var.set("WiFi: Disconnected")
            elif "Connecting" in line:
                wifi_var.set("WiFi: Connecting...")
        except (NameError, ValueError):
            pass

    elif "[SYSTEM]" in line:
        try:
            msg = line.split("[SYSTEM]", 1)[1].strip()
            system_var.set(msg)
        except (NameError, ValueError):
            pass

    # ===== VFD =====
    elif "Set frequency" in line:
        val = line.split("Set frequency")[1].strip()

        if "OK" in line:
            freq = val.replace("OK", "").strip()
            vfd_var.set(freq + " Hz (OK) ")

        elif "NG" in line:
            freq = val.replace("NG", "").strip()
            vfd_var.set(freq + " Hz (FAIL) ")

# ================= SERIAL LOOP =================
def read_serial():
    global ser

    if ser:
        try:
            if not ser.is_open:
                close_serial()
                port_var.set("---")
                return

            while ser.in_waiting:
                line = ser.readline().decode(errors="ignore").strip()
                if line:
                    append_log(line)
                    parse_line(line)

        except:
            close_serial()
            port_var.set("---")

    root.after(READ_INTERVAL_MS, read_serial)

# ================= TOOLTIP =================
class Tooltip:
    def __init__(self, widget, text):
        self.widget = widget
        self.text = text
        widget.bind("<Enter>", self.show)
        widget.bind("<Leave>", self.hide)

    def show(self, e):
        self.tip = tk.Toplevel(self.widget)
        self.tip.wm_overrideredirect(True)
        self.tip.geometry(f"+{e.x_root+10}+{e.y_root+10}")
        tk.Label(self.tip, text=self.text, bg="yellow").pack()

    def hide(self, e):
        if hasattr(self, "tip"):
            self.tip.destroy()

# ================= UI =================
root = tk.Tk()
root.title("VFD Parameter Tuner")
root.geometry("560x740")
root.configure(bg="black")

# --- Window icon ---
try:
    _icon_img = tk.PhotoImage(file=resource_path("adjustments.png"))
    root.iconphoto(True, _icon_img)
except Exception:
    pass

# HEADER
header = tk.Frame(root, bg="black")
header.pack(fill="x")

mode_var = tk.StringVar(value="--")
port_var = tk.StringVar(value="---")
wifi_var = tk.StringVar(value="WiFi: --")
system_var = tk.StringVar(value="")

tk.Label(header, text="MODE:", fg="white", bg="black").pack(side="left")
tk.Label(header, textvariable=mode_var, fg="cyan", bg="black").pack(side="left")

tk.Label(header, textvariable=port_var, fg="orange", bg="black").pack(side="right")

tk.Frame(root, height=2, bg="white").pack(fill="x")

# SERVER INFO BAR
info_bar = tk.Frame(root, bg="#0a0a2e")
info_bar.pack(fill="x")
tk.Label(info_bar, textvariable=wifi_var, fg="#00aaff", bg="#0a0a2e",
         font=("Consolas", 10, "bold")).pack(side="left", padx=8, pady=2)
tk.Label(info_bar, textvariable=system_var, fg="#88cc88", bg="#0a0a2e",
         font=("Consolas", 9)).pack(side="right", padx=8, pady=2)

tk.Frame(root, height=1, bg="#334").pack(fill="x")

# DISPLAY
frame = tk.Frame(root, bg="black")
frame.pack(pady=10)

dist_var = tk.StringVar(value="--")
boost_level_var = tk.StringVar(value="--")
boost_hold_var = tk.StringVar(value="--")

tk.Label(frame, text="Distance", fg="white", bg="black").grid(row=0, column=0)
tk.Label(frame, text="Boost Lv", fg="white", bg="black").grid(row=0, column=1)
tk.Label(frame, text="Boost Hold", fg="white", bg="black").grid(row=0, column=2)

tk.Label(frame, textvariable=dist_var, fg="cyan", bg="black",
         font=("Consolas", 18, "bold")).grid(row=1, column=0)

tk.Label(frame, textvariable=boost_level_var, fg="yellow", bg="black",
         font=("Consolas", 18, "bold")).grid(row=1, column=1)

tk.Label(frame, textvariable=boost_hold_var, fg="#ffcc66", bg="black",
         font=("Consolas", 18, "bold")).grid(row=1, column=2)

# TARGET
target_var = tk.StringVar(value="--")
tk.Label(root, text="Target Frequency", fg="white", bg="black").pack()
tk.Label(root, textvariable=target_var, fg="lime", bg="black",
         font=("Consolas", 16)).pack()

# VFD
vfd_var = tk.StringVar(value="--")
tk.Label(root, text="VFD", fg="white", bg="black").pack()
tk.Label(root, textvariable=vfd_var, fg="orange", bg="black", font=("Consolas", 16)).pack()

# STATUS
status_var = tk.StringVar(value="")
status_label = tk.Label(root, textvariable=status_var, fg="#ff6666",
                        bg="black", font=("Consolas", 11))
status_label.pack()

# PARAM
param = tk.Frame(root, bg="#111")
param.pack(fill="x", pady=10)

def row(label, desc):
    f = tk.Frame(param, bg="#111")
    f.pack(anchor="w", padx=10, pady=2)

    tk.Label(f, text=label, fg="white", bg="#111", width=8, anchor="w").pack(side="left")
    e = tk.Entry(f, width=10, bg="#111111", fg="white",
                 insertbackground="white", disabledbackground="#222")
    e.pack(side="left", padx=5)
    tk.Label(f, text=desc, fg="#888", bg="#111").pack(side="left")

    e.bind("<FocusOut>", lambda event: update_button_states())
    Tooltip(e, desc)
    return e

e_udmin = row("UDMIN", "Khoảng cách đầy (cm)")
e_udmax = row("UDMAX", "Khoảng cách rỗng (cm)")
e_ufmin = row("UFMIN", "Tần số min (Hz)")
e_ufmax = row("UFMAX", "Tần số max (Hz)")
e_boot  = row("BOOST", "Phần trăm tăng tốc (%)")
e_boosttime = row("BOOST T", "Thời gian boost (s)")
e_boost_l2 = row("BOOST L2", "% tăng tốc cấp 2")
e_boost_l3 = row("BOOST L3", "% tăng tốc cấp 3")
e_boost_h2 = row("HOLD L2", "Thời gian giữ cấp 2 (s)")
e_boost_h3 = row("HOLD L3", "Thời gian giữ cấp 3 (s)")
e_boost_e2 = row("ESC L2", "Thời gian kích liên tục lên cấp 2 (s)")
e_boost_e3 = row("ESC L3", "Thời gian kích liên tục lên cấp 3 (s)")
e_boost_decay = row("DECAY", "Thời gian ổn định để giảm 1 cấp (s)")

entries = [e_udmin, e_udmax, e_ufmin, e_ufmax, e_boot, e_boosttime]

action_row = tk.Frame(param, bg="#111")
action_row.pack(pady=5)

btn_apply = tk.Button(action_row, text="Apply", command=send_param,
                      bg="green", fg="white", width=12, state="disabled")
btn_apply.pack(side="left", padx=4)

btn_reset = tk.Button(action_row, text="RESET ESP", command=reset_esp,
                      bg="#8b0000", fg="white", width=12, state="disabled")
btn_reset.pack(side="left", padx=4)

set_app_state(STATE_DISCONNECTED)

# LOG
log = tk.Text(root, height=6, bg="black", fg="white")
log.pack(fill="x")

# FOOTER
footer = tk.Label(root,
                  text="Developed by Hung - Staff FA",
                  fg="#666",
                  bg="black",
                  font=("Consolas", 9))
footer.pack(side="bottom", anchor="e", padx=10, pady=3)

# START
root.after(READ_INTERVAL_MS, read_serial)
root.after(SCAN_INTERVAL_MS, auto_scan)

root.mainloop()
close_serial()