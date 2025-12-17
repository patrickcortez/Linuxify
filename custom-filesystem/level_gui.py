# level_gui.py
# Linuxify LevelFS v2 Explorer GUI
# Modern UI with disk selection and level navigation

import tkinter as tk
from tkinter import ttk, messagebox, simpledialog
import sys
import string
import ctypes
import fs_driver

try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)
except:
    try:
        ctypes.windll.user32.SetProcessDPIAware()
    except:
        pass


def scan_levelfs_disks():
    valid_disks = []
    for letter in string.ascii_uppercase:
        try:
            fs = fs_driver.FileSystem(letter)
            if fs.mount():
                info = {
                    'letter': letter,
                    'name': fs.sb.volume_name or "Unnamed",
                    'total_mb': (fs.sb.total_clusters * fs_driver.CLUSTER_SIZE) / (1024*1024),
                    'free_mb': (fs.sb.total_free_clusters * fs_driver.CLUSTER_SIZE) / (1024*1024),
                }
                valid_disks.append(info)
                fs.close()
        except:
            pass
    return valid_disks


class DiskSelectionDialog:
    def __init__(self, parent):
        self.result = None
        self.disks = []
        self.dialog = tk.Toplevel(parent)
        self.dialog.title("Select LevelFS Volume")
        self.dialog.geometry("550x450")
        self.dialog.configure(bg="#1a1a2e")
        self.dialog.resizable(False, False)
        self.dialog.transient(parent)
        self.dialog.grab_set()
        self.center_window()
        self.setup_ui()
        self.dialog.after(100, self.scan_disks)
        parent.wait_window(self.dialog)
    
    def center_window(self):
        w, h = 550, 450
        x = (self.dialog.winfo_screenwidth() - w) // 2
        y = (self.dialog.winfo_screenheight() - h) // 2
        self.dialog.geometry(f"{w}x{h}+{x}+{y}")
    
    def setup_ui(self):
        header = tk.Frame(self.dialog, bg="#16213e", height=90)
        header.pack(fill=tk.X)
        header.pack_propagate(False)
        
        tk.Label(header, text="🗂", font=("Segoe UI Emoji", 36), bg="#16213e", fg="#4cc9f0").pack(side=tk.LEFT, padx=25, pady=15)
        title_frame = tk.Frame(header, bg="#16213e")
        title_frame.pack(side=tk.LEFT, fill=tk.Y, pady=20)
        tk.Label(title_frame, text="LevelFS Explorer", font=("Segoe UI", 18, "bold"), bg="#16213e", fg="#fff").pack(anchor=tk.W)
        tk.Label(title_frame, text="Select a volume to explore", font=("Segoe UI", 11), bg="#16213e", fg="#888").pack(anchor=tk.W)
        
        self.status_label = tk.Label(self.dialog, text="Scanning...", bg="#1a1a2e", fg="#4cc9f0", font=("Segoe UI", 10))
        self.status_label.pack(pady=15)
        
        list_frame = tk.Frame(self.dialog, bg="#0f0f23", bd=1, relief=tk.SOLID)
        list_frame.pack(fill=tk.BOTH, expand=True, padx=25, pady=5)
        self.disk_list = tk.Listbox(list_frame, font=("Consolas", 12), bg="#0f0f23", fg="#e0e0e0",
                                   selectbackground="#4361ee", selectforeground="#fff",
                                   highlightthickness=0, bd=0, activestyle='none')
        self.disk_list.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        self.disk_list.bind('<Double-1>', lambda e: self.on_select())
        
        btn_frame = tk.Frame(self.dialog, bg="#1a1a2e")
        btn_frame.pack(fill=tk.X, padx=25, pady=20)
        self.btn_open = tk.Button(btn_frame, text="  Open  ", command=self.on_select, bg="#4361ee", fg="#fff",
                                 font=("Segoe UI", 11, "bold"), relief=tk.FLAT, padx=20, pady=10, state=tk.DISABLED)
        self.btn_open.pack(side=tk.RIGHT)
        tk.Button(btn_frame, text="🔄 Rescan", command=self.scan_disks, bg="#333", fg="#fff",
                 font=("Segoe UI", 10), relief=tk.FLAT, padx=15, pady=10).pack(side=tk.RIGHT, padx=10)
        tk.Button(btn_frame, text="Enter Manually...", command=self.manual_entry, bg="#333", fg="#fff",
                 font=("Segoe UI", 10), relief=tk.FLAT, padx=15, pady=10).pack(side=tk.LEFT)
    
    def scan_disks(self):
        self.btn_open.config(state=tk.DISABLED)
        self.disk_list.delete(0, tk.END)
        self.status_label.config(text="Scanning drives...")
        self.dialog.update()
        self.disks = scan_levelfs_disks()
        
        if not self.disks:
            self.status_label.config(text="No LevelFS volumes found", fg="#ff6b6b")
            self.disk_list.insert(tk.END, "   No LevelFS volumes detected.")
            self.disk_list.insert(tk.END, "   Run as Administrator for disk access.")
        else:
            self.status_label.config(text=f"Found {len(self.disks)} volume(s)", fg="#4cc9f0")
            for d in self.disks:
                used = d['total_mb'] - d['free_mb']
                pct = (used / d['total_mb'] * 100) if d['total_mb'] > 0 else 0
                self.disk_list.insert(tk.END, f"   {d['letter']}:   {d['name']:<16}   {used:.0f}/{d['total_mb']:.0f} MB ({pct:.0f}%)")
            self.disk_list.select_set(0)
            self.btn_open.config(state=tk.NORMAL)
    
    def manual_entry(self):
        letter = simpledialog.askstring("Drive Letter", "Enter drive letter (e.g. D):", parent=self.dialog)
        if letter:
            self.result = letter.upper().replace(":", "")
            self.dialog.destroy()
    
    def on_select(self):
        sel = self.disk_list.curselection()
        if sel and self.disks and sel[0] < len(self.disks):
            self.result = self.disks[sel[0]]['letter']
            self.dialog.destroy()


class LevelSelectionDialog:
    def __init__(self, parent, versions, folder_name):
        self.result = None
        self.versions = versions
        self.dialog = tk.Toplevel(parent)
        self.dialog.title("Select Level")
        self.dialog.geometry("400x320")
        self.dialog.configure(bg="#1a1a2e")
        self.dialog.resizable(False, False)
        self.dialog.transient(parent)
        self.dialog.grab_set()
        
        w, h = 400, 320
        x = (self.dialog.winfo_screenwidth() - w) // 2
        y = (self.dialog.winfo_screenheight() - h) // 2
        self.dialog.geometry(f"{w}x{h}+{x}+{y}")
        
        header = tk.Frame(self.dialog, bg="#16213e", height=60)
        header.pack(fill=tk.X)
        header.pack_propagate(False)
        tk.Label(header, text=f"📁 {folder_name}", font=("Segoe UI", 14, "bold"), bg="#16213e", fg="#fff").pack(side=tk.LEFT, padx=20, pady=15)
        
        tk.Label(self.dialog, text="This folder has multiple levels:", bg="#1a1a2e", fg="#888", font=("Segoe UI", 10)).pack(pady=10)
        
        list_frame = tk.Frame(self.dialog, bg="#0f0f23", bd=1, relief=tk.SOLID)
        list_frame.pack(fill=tk.BOTH, expand=True, padx=20, pady=5)
        self.level_list = tk.Listbox(list_frame, font=("Consolas", 12), bg="#0f0f23", fg="#e0e0e0",
                                     selectbackground="#4361ee", selectforeground="#fff", highlightthickness=0, bd=0)
        self.level_list.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        for v in versions:
            self.level_list.insert(tk.END, f"   {v.name}")
        self.level_list.select_set(0)
        self.level_list.bind('<Double-1>', lambda e: self.on_ok())
        
        btn_frame = tk.Frame(self.dialog, bg="#1a1a2e")
        btn_frame.pack(fill=tk.X, padx=20, pady=15)
        tk.Button(btn_frame, text="Open", command=self.on_ok, bg="#4361ee", fg="#fff",
                 font=("Segoe UI", 10, "bold"), relief=tk.FLAT, padx=20, pady=8).pack(side=tk.RIGHT)
        tk.Button(btn_frame, text="Cancel", command=self.dialog.destroy, bg="#333", fg="#fff",
                 font=("Segoe UI", 10), relief=tk.FLAT, padx=15, pady=8).pack(side=tk.RIGHT, padx=10)
        
        parent.wait_window(self.dialog)
    
    def on_ok(self):
        sel = self.level_list.curselection()
        if sel:
            self.result = self.versions[sel[0]]
            self.dialog.destroy()


class LevelExplorerApp:
    def __init__(self, root, disk_letter):
        self.root = root
        self.root.title(f"LevelFS Explorer - {disk_letter}:")
        self.root.geometry("1100x750")
        self.root.configure(bg="#0f0f23")
        self.root.minsize(900, 600)
        
        self.setup_styles()

        self.fs = fs_driver.FileSystem(disk_letter)
        if not self.fs.mount():
            messagebox.showerror("Error", f"Failed to mount {disk_letter}:")
            sys.exit(1)
        
        self.history = []
        self.current_path = "/"
        self.current_version_name = "master"
        self.current_content_cluster = 0
        
        versions = self.fs.list_versions(self.fs.sb.root_dir_cluster)
        master = next((v for v in versions if v.name == "master"), None)
        if not master and versions:
            master = versions[0]
        
        if master:
            self.current_version_name = master.name
            self.current_content_cluster = master.content_cluster
        else:
            messagebox.showerror("Error", "No levels found in root directory")
            sys.exit(1)

        self.setup_ui()
        self.refresh()
    
    def setup_styles(self):
        style = ttk.Style()
        style.theme_use('clam')
        style.configure("Custom.Treeview", background="#0f0f23", fieldbackground="#0f0f23", 
                       foreground="#e0e0e0", rowheight=36, font=("Segoe UI", 11))
        style.configure("Custom.Treeview.Heading", background="#1a1a2e", foreground="#4cc9f0", 
                       font=("Segoe UI", 10, "bold"), relief=tk.FLAT)
        style.map("Custom.Treeview", background=[("selected", "#4361ee")], foreground=[("selected", "#fff")])
        style.configure("Custom.Horizontal.TProgressbar", background="#4cc9f0", troughcolor="#1a1a2e")

    def setup_ui(self):
        header = tk.Frame(self.root, bg="#16213e", height=80)
        header.pack(fill=tk.X)
        header.pack_propagate(False)
        
        tk.Label(header, text="🗂", font=("Segoe UI Emoji", 32), bg="#16213e", fg="#4cc9f0").pack(side=tk.LEFT, padx=20, pady=10)
        
        title_frame = tk.Frame(header, bg="#16213e")
        title_frame.pack(side=tk.LEFT, fill=tk.Y, pady=15)
        tk.Label(title_frame, text="LevelFS Explorer", font=("Segoe UI", 16, "bold"), bg="#16213e", fg="#fff").pack(anchor=tk.W)
        self.lbl_subtitle = tk.Label(title_frame, text="/", font=("Consolas", 11), bg="#16213e", fg="#4cc9f0")
        self.lbl_subtitle.pack(anchor=tk.W)
        
        usage_frame = tk.Frame(header, bg="#16213e")
        usage_frame.pack(side=tk.RIGHT, padx=25, pady=15)
        self.lbl_stats = tk.Label(usage_frame, text="", bg="#16213e", fg="#888", font=("Segoe UI", 10))
        self.lbl_stats.pack(anchor=tk.E)
        self.progress_var = tk.DoubleVar()
        self.progress_bar = ttk.Progressbar(usage_frame, variable=self.progress_var, maximum=100, length=200, 
                                           style="Custom.Horizontal.TProgressbar")
        self.progress_bar.pack(anchor=tk.E, pady=5)

        toolbar = tk.Frame(self.root, bg="#1a1a2e", height=55)
        toolbar.pack(fill=tk.X)
        toolbar.pack_propagate(False)
        
        btn_style = {"bg": "#252545", "fg": "#fff", "font": ("Segoe UI", 10), "relief": tk.FLAT, "padx": 15, "pady": 8, "cursor": "hand2"}
        tk.Button(toolbar, text="⬆ Up", command=self.go_up, **btn_style).pack(side=tk.LEFT, padx=8, pady=10)
        tk.Button(toolbar, text="🔄 Refresh", command=self.refresh, **btn_style).pack(side=tk.LEFT, padx=8, pady=10)
        tk.Button(toolbar, text="🏠 Home", command=self.go_home, **btn_style).pack(side=tk.LEFT, padx=8, pady=10)
        
        self.lbl_path = tk.Label(toolbar, text="/", bg="#1a1a2e", fg="#4cc9f0", font=("Consolas", 12))
        self.lbl_path.pack(side=tk.LEFT, padx=25)
        
        self.lbl_level = tk.Label(toolbar, text=f" {self.current_version_name} ", bg="#4361ee", fg="#fff", 
                                 font=("Segoe UI", 10, "bold"), padx=12, pady=4)
        self.lbl_level.pack(side=tk.LEFT, padx=8)

        tree_container = tk.Frame(self.root, bg="#0f0f23")
        tree_container.pack(fill=tk.BOTH, expand=True, padx=20, pady=15)
        
        columns = ("size", "type", "cluster")
        self.tree = ttk.Treeview(tree_container, columns=columns, show='tree headings', style="Custom.Treeview")
        self.tree.heading("#0", text="  Name", anchor=tk.W)
        self.tree.heading("size", text="Size")
        self.tree.heading("type", text="Type")
        self.tree.heading("cluster", text="Cluster")
        self.tree.column("#0", width=500, anchor=tk.W)
        self.tree.column("size", width=120, anchor=tk.E)
        self.tree.column("type", width=120, anchor=tk.CENTER)
        self.tree.column("cluster", width=100, anchor=tk.E)
        
        scrollbar = ttk.Scrollbar(tree_container, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscroll=scrollbar.set)
        self.tree.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)
        
        self.tree.bind("<Double-1>", self.on_double_click)
        self.tree.bind("<Return>", self.on_double_click)

        status = tk.Frame(self.root, bg="#4361ee", height=32)
        status.pack(fill=tk.X, side=tk.BOTTOM)
        status.pack_propagate(False)
        self.lbl_status = tk.Label(status, text="Ready", bg="#4361ee", fg="#fff", font=("Segoe UI", 10))
        self.lbl_status.pack(side=tk.LEFT, padx=20, pady=6)
        self.lbl_items = tk.Label(status, text="", bg="#4361ee", fg="#fff", font=("Segoe UI", 10))
        self.lbl_items.pack(side=tk.RIGHT, padx=20, pady=6)

    def refresh(self):
        total = self.fs.sb.total_clusters * fs_driver.CLUSTER_SIZE
        free = self.fs.sb.total_free_clusters * fs_driver.CLUSTER_SIZE
        used = total - free
        percent = (used / total * 100) if total > 0 else 0
        self.lbl_stats.config(text=f"{used/(1024*1024):.0f} / {total/(1024*1024):.0f} MB")
        self.progress_var.set(percent)
        
        self.lbl_path.config(text=self.current_path)
        self.lbl_subtitle.config(text=self.current_path)
        self.lbl_level.config(text=f" {self.current_version_name} ")
        
        for item in self.tree.get_children():
            self.tree.delete(item)

        if self.current_content_cluster == 0:
            self.lbl_status.config(text="Empty")
            return
        
        entries = self.fs.list_dir(self.current_content_cluster)
        entries.sort(key=lambda x: (0 if x.type == fs_driver.TYPE_LEVELED_DIR else 1, x.name.lower()))

        icons = {fs_driver.TYPE_FILE: "📄", fs_driver.TYPE_LEVELED_DIR: "📁", 
                 fs_driver.TYPE_SYMLINK: "🔗", fs_driver.TYPE_HARDLINK: "🔗", fs_driver.TYPE_LEVEL_MOUNT: "📌"}

        for e in entries:
            icon = icons.get(e.type, "❓")
            size_str = self.format_size(e.size) if e.type == fs_driver.TYPE_FILE else "-"
            self.tree.insert("", tk.END, iid=e.name, text=f"   {icon}  {e.name}", 
                           values=(size_str, e.get_type_str(), e.start_cluster))
        
        self.entries_map = {e.name: e for e in entries}
        self.lbl_items.config(text=f"{len(entries)} items")
        self.lbl_status.config(text="Ready")

    def format_size(self, size):
        if size < 1024: return f"{size} B"
        if size < 1024**2: return f"{size/1024:.1f} KB"
        if size < 1024**3: return f"{size/1024**2:.1f} MB"
        return f"{size/1024**3:.1f} GB"

    def on_double_click(self, event):
        sel = self.tree.selection()
        if not sel: return
        entry = self.entries_map.get(sel[0])
        if not entry: return
        
        if entry.type == fs_driver.TYPE_LEVELED_DIR:
            self.enter_folder(entry)
        elif entry.type == fs_driver.TYPE_FILE:
            self.view_file(entry)
        elif entry.type == fs_driver.TYPE_SYMLINK:
            target = self.fs.read_symlink_target(entry.start_cluster)
            messagebox.showinfo("Symlink", f"Target: {target}")

    def enter_folder(self, entry):
        versions = self.fs.list_versions(entry.start_cluster)
        
        if not versions:
            messagebox.showinfo("Empty", "This folder has no levels.")
            return
        
        if len(versions) == 1:
            target = versions[0]
        else:
            dialog = LevelSelectionDialog(self.root, versions, entry.name)
            if not dialog.result:
                return
            target = dialog.result
        
        if target.content_cluster == 0:
            messagebox.showinfo("Empty", f"Level '{target.name}' is empty.")
            return
        
        self.history.append((self.current_path, self.current_content_cluster, self.current_version_name))
        self.current_content_cluster = target.content_cluster
        self.current_version_name = target.name
        sep = "" if self.current_path == "/" else "/"
        self.current_path += sep + entry.name
        self.refresh()

    def go_up(self):
        if not self.history: return
        path, cluster, version = self.history.pop()
        self.current_path = path
        self.current_content_cluster = cluster
        self.current_version_name = version
        self.refresh()
    
    def go_home(self):
        self.history.clear()
        self.current_path = "/"
        versions = self.fs.list_versions(self.fs.sb.root_dir_cluster)
        master = next((v for v in versions if v.name == "master"), versions[0] if versions else None)
        if master:
            self.current_version_name = master.name
            self.current_content_cluster = master.content_cluster
        self.refresh()

    def view_file(self, entry):
        size = min(entry.size, 1024*1024)
        data = self.fs.read_file(entry.start_cluster, size)
        try:
            text = data.decode('utf-8')
        except:
            text = f"<Binary: {len(data)} bytes>\n" + " ".join(f"{b:02x}" for b in data[:512])
        
        top = tk.Toplevel(self.root)
        top.title(f"📄 {entry.name}")
        top.geometry("900x650")
        top.configure(bg="#0f0f23")
        
        txt = tk.Text(top, bg="#1a1a2e", fg="#e0e0e0", font=("Consolas", 12), wrap=tk.WORD, relief=tk.FLAT, padx=20, pady=20)
        txt.pack(fill=tk.BOTH, expand=True, padx=15, pady=15)
        txt.insert(tk.END, text)
        txt.config(state=tk.DISABLED)


def main():
    root = tk.Tk()
    root.withdraw()
    
    if len(sys.argv) >= 2:
        disk = sys.argv[1].upper().replace(":", "")
    else:
        dialog = DiskSelectionDialog(root)
        if not dialog.result:
            sys.exit(0)
        disk = dialog.result
    
    root.deiconify()
    LevelExplorerApp(root, disk)
    root.mainloop()


if __name__ == "__main__":
    main()
