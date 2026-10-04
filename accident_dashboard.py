import json
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox
import serial
import serial.tools.list_ports


BAUD_RATE = 115200


class AccidentDashboard:
    def __init__(self, root):
        self.root = root
        self.root.title("Accident Detection - Live Dashboard")
        self.root.geometry("920x680")
        self.root.minsize(850, 620)

        self.ser = None
        self.reader_thread = None
        self.running = False
        self.rx_queue = queue.Queue()

        self.status_var = tk.StringVar(value="Disconnected")
        self.vibration_var = tk.StringVar(value="--")
        self.sound_var = tk.StringVar(value="--")
        self.distance_var = tk.StringVar(value="--")
        self.confidence_var = tk.StringVar(value="--")
        self.severity_var = tk.StringVar(value="--")
        self.state_var = tk.StringVar(value="--")
        self.sms_var = tk.StringVar(value="--")

        self.build_ui()
        self.refresh_ports()
        self.process_queue()

        self.root.protocol("WM_DELETE_WINDOW", self.on_close)

    # ---------------------------------------------------------
    # UI
    # ---------------------------------------------------------
    def build_ui(self):
        style = ttk.Style()
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass

        header = tk.Frame(self.root, bg="#0B4F81", height=85)
        header.pack(fill="x")
        header.pack_propagate(False)

        tk.Label(
            header,
            text="ACCIDENT DETECTION SYSTEM",
            bg="#0B4F81",
            fg="white",
            font=("Segoe UI", 20, "bold")
        ).pack(pady=(14, 0))

        tk.Label(
            header,
            text="ESP32 • Multi-Sensor Monitoring • GSM Emergency Alert",
            bg="#0B4F81",
            fg="#DCEEFF",
            font=("Segoe UI", 10)
        ).pack()

        # Connection section
        connection = ttk.LabelFrame(self.root, text="ESP32 Connection", padding=10)
        connection.pack(fill="x", padx=16, pady=12)

        tk.Label(connection, text="COM Port:").grid(row=0, column=0, padx=6)
        self.port_combo = ttk.Combobox(connection, width=14, state="readonly")
        self.port_combo.grid(row=0, column=1, padx=6)

        ttk.Button(
            connection,
            text="Refresh",
            command=self.refresh_ports
        ).grid(row=0, column=2, padx=6)

        self.connect_button = ttk.Button(
            connection,
            text="Connect",
            command=self.toggle_connection
        )
        self.connect_button.grid(row=0, column=3, padx=6)

        tk.Label(
            connection,
            textvariable=self.status_var,
            font=("Segoe UI", 10, "bold")
        ).grid(row=0, column=4, padx=14)

        # Sensor cards
        sensor_frame = ttk.LabelFrame(self.root, text="Live Sensor Data", padding=12)
        sensor_frame.pack(fill="x", padx=16, pady=6)

        cards = [
            ("VIBRATION", self.vibration_var),
            ("SOUND", self.sound_var),
            ("DISTANCE", self.distance_var),
            ("CONFIDENCE", self.confidence_var),
        ]

        for i, (title, variable) in enumerate(cards):
            box = tk.Frame(
                sensor_frame,
                bg="#EAF4FB",
                highlightbackground="#B7D7EA",
                highlightthickness=1
            )
            box.grid(row=0, column=i, padx=7, sticky="nsew")

            tk.Label(
                box,
                text=title,
                bg="#EAF4FB",
                fg="#0B4F81",
                font=("Segoe UI", 10, "bold")
            ).pack(pady=(12, 4))

            tk.Label(
                box,
                textvariable=variable,
                bg="#EAF4FB",
                fg="#222222",
                font=("Segoe UI", 20, "bold")
            ).pack(pady=(0, 12))

            sensor_frame.columnconfigure(i, weight=1)

        # Main status panel
        status_frame = ttk.LabelFrame(self.root, text="System Status", padding=16)
        status_frame.pack(fill="x", padx=16, pady=10)

        tk.Label(
            status_frame,
            text="STATE",
            font=("Segoe UI", 10, "bold")
        ).grid(row=0, column=0, sticky="w", padx=10, pady=8)

        self.state_label = tk.Label(
            status_frame,
            textvariable=self.state_var,
            font=("Segoe UI", 18, "bold"),
            fg="#0B4F81"
        )
        self.state_label.grid(row=0, column=1, sticky="w", padx=10)

        tk.Label(
            status_frame,
            text="SEVERITY",
            font=("Segoe UI", 10, "bold")
        ).grid(row=1, column=0, sticky="w", padx=10, pady=8)

        self.severity_label = tk.Label(
            status_frame,
            textvariable=self.severity_var,
            font=("Segoe UI", 18, "bold"),
            fg="#008000"
        )
        self.severity_label.grid(row=1, column=1, sticky="w", padx=10)

        tk.Label(
            status_frame,
            text="SMS",
            font=("Segoe UI", 10, "bold")
        ).grid(row=2, column=0, sticky="w", padx=10, pady=8)

        tk.Label(
            status_frame,
            textvariable=self.sms_var,
            font=("Segoe UI", 12, "bold")
        ).grid(row=2, column=1, sticky="w", padx=10)

        # Controls
        controls = ttk.LabelFrame(self.root, text="Demo Controls", padding=12)
        controls.pack(fill="x", padx=16, pady=6)

        ttk.Button(
            controls,
            text="RUN DEMO ALERT",
            command=self.run_demo
        ).pack(side="left", padx=8)

        ttk.Button(
            controls,
            text="RESET SYSTEM",
            command=self.reset_system
        ).pack(side="left", padx=8)

        ttk.Button(
            controls,
            text="GET STATUS",
            command=self.get_status
        ).pack(side="left", padx=8)

        tk.Label(
            controls,
            text="Choose the mode",
            fg="#8A4500"
        ).pack(side="left", padx=14)

        # Log
        log_frame = ttk.LabelFrame(self.root, text="Serial Log", padding=8)
        log_frame.pack(fill="both", expand=True, padx=16, pady=(6, 16))

        self.log_text = tk.Text(
            log_frame,
            height=10,
            wrap="word",
            font=("Consolas", 9)
        )
        self.log_text.pack(side="left", fill="both", expand=True)

        scrollbar = ttk.Scrollbar(
            log_frame,
            orient="vertical",
            command=self.log_text.yview
        )
        scrollbar.pack(side="right", fill="y")
        self.log_text.configure(yscrollcommand=scrollbar.set)

    # ---------------------------------------------------------
    # COM port handling
    # ---------------------------------------------------------
    def refresh_ports(self):
        ports = [p.device for p in serial.tools.list_ports.comports()]
        self.port_combo["values"] = ports

        if ports:
            # Select COM7 if it exists, otherwise first port.
            if "COM7" in ports:
                self.port_combo.set("COM7")
            else:
                self.port_combo.current(0)

    def toggle_connection(self):
        if self.ser and self.ser.is_open:
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        port = self.port_combo.get().strip()

        if not port:
            messagebox.showwarning(
                "Port Required",
                "Select the ESP32 COM port first."
            )
            return

        try:
            self.ser = serial.Serial(
                port,
                BAUD_RATE,
                timeout=0.5
            )

            time.sleep(1.5)

            self.running = True
            self.reader_thread = threading.Thread(
                target=self.read_serial,
                daemon=True
            )
            self.reader_thread.start()

            self.connect_button.config(text="Disconnect")
            self.status_var.set(f"Connected: {port}")
            self.log("CONNECTED TO " + port)

        except serial.SerialException as exc:
            messagebox.showerror(
                "Connection Error",
                str(exc)
            )

    def disconnect(self):
        self.running = False

        if self.ser:
            try:
                self.ser.close()
            except serial.SerialException:
                pass

        self.ser = None
        self.connect_button.config(text="Connect")
        self.status_var.set("Disconnected")
        self.log("DISCONNECTED")

    # ---------------------------------------------------------
    # Serial
    # ---------------------------------------------------------
    def read_serial(self):
        while self.running and self.ser:
            try:
                line = self.ser.readline().decode(
                    "utf-8",
                    errors="ignore"
                ).strip()

                if line:
                    self.rx_queue.put(line)

            except (serial.SerialException, OSError):
                self.rx_queue.put("ERROR|SERIAL_CONNECTION_LOST")
                break

    def process_queue(self):
        while True:
            try:
                line = self.rx_queue.get_nowait()
            except queue.Empty:
                break

            self.handle_line(line)

        self.root.after(100, self.process_queue)

    def handle_line(self, line):
        self.log(line)

        if line.startswith("LIVE,"):
            parts = line.split(",")

            if len(parts) >= 8:
                _, vib, sound, distance, confidence, state, severity, sms = parts[:8]

                self.vibration_var.set(
                    "DETECTED" if vib == "1" else "NORMAL"
                )
                self.sound_var.set(sound)

                try:
                    dist = float(distance)
                    self.distance_var.set(
                        "--" if dist < 0 else f"{dist:.1f} cm"
                    )
                except ValueError:
                    self.distance_var.set("--")

                self.confidence_var.set(f"{confidence}%")
                self.state_var.set(state.replace("_", " "))
                self.severity_var.set(severity)
                self.sms_var.set(
                    "SENT" if sms == "1" else "NOT SENT"
                )

                self.update_status_colors(state, severity)

        elif line.startswith("SMS_SENT|"):
            sent = line.split("|", 1)[1].strip()
            self.sms_var.set(
                "SENT" if sent == "1" else "FAILED"
            )

        elif line.startswith("GSM|"):
            pass

    def update_status_colors(self, state, severity):
        if state in ("ACCIDENT", "DEMO ALERT"):
            self.state_label.config(fg="#C62828")
        else:
            self.state_label.config(fg="#0B4F81")

        severity_colors = {
            "HIGH": "#C62828",
            "MEDIUM": "#D97706",
            "LOW": "#B58900",
            "NONE": "#008000",
        }

        self.severity_label.config(
            fg=severity_colors.get(severity, "#333333")
        )

    def send_command(self, command):
        if not self.ser or not self.ser.is_open:
            messagebox.showwarning(
                "Not Connected",
                "Connect to the ESP32 first."
            )
            return

        try:
            self.ser.write(command.encode("ascii"))
            self.ser.flush()
        except (serial.SerialException, OSError) as exc:
            messagebox.showerror(
                "Serial Error",
                str(exc)
            )

    # ---------------------------------------------------------
    # Controls
    # ---------------------------------------------------------
    def run_demo(self):
        answer = messagebox.askyesno(
            "Run Demo Alert",
            "This triggers a controlled software demo event.\n\n"
            "It may send an SMS if SMS is enabled in the ESP32 code.\n\n"
            "Continue?"
        )

        if answer:
            self.send_command("D")
            self.log("COMMAND -> DEMO ALERT")

    def reset_system(self):
        self.send_command("R")
        self.log("COMMAND -> RESET")

    def get_status(self):
        self.send_command("S")
        self.log("COMMAND -> STATUS")

    # ---------------------------------------------------------
    # Logging / shutdown
    # ---------------------------------------------------------
    def log(self, text):
        timestamp = time.strftime("%H:%M:%S")
        self.log_text.insert(
            "end",
            f"[{timestamp}] {text}\n"
        )
        self.log_text.see("end")

    def on_close(self):
        self.disconnect()
        self.root.destroy()


if __name__ == "__main__":
    root = tk.Tk()
    app = AccidentDashboard(root)
    root.mainloop()
