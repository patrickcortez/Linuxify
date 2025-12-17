import tkinter as tk
from tkinter import ttk, messagebox, simpledialog
import sys
import fs_driver

class LevelExplorerApp:
    def __init__(self, root, image_path):
        self.root = root
        self.root.title("Linuxify LevelFS Explorer")
        self.root.geometry("800x600")
        
        # Style
        style = ttk.Style()
        style.theme_use('clam')
        style.configure("Treeview", background="#2b2b2b", 
                       fieldbackground="#2b2b2b", foreground="white", rowheight=25)
        style.configure("Treeview.Heading", background="#3c3f41", foreground="white")
        self.root.configure(bg="#2b2b2b")

        self.fs = fs_driver.FileSystem(image_path)
        if not self.fs.mount():
            messagebox.showerror("Error", "Failed to mount image.")
            sys.exit(1)
            
        self.history = [] # Stack of (path, dir_cluster, content_cluster, version_name)
        self.current_path = "/"
        self.current_version = "master"
        self.current_dir_cluster = self.fs.sb.root_dir_cluster
        
        # Initial Content Cluster (master of root)
        self.current_content_cluster = self.fs.find_level(self.current_dir_cluster, "master")
        if self.current_content_cluster == 0:
            # Fallback to first active
            levels = self.fs.list_levels(self.current_dir_cluster)
            if levels:
                self.current_content_cluster = levels[0].content_cluster
                self.current_version = levels[0].name
            else:
                 messagebox.showerror("Error", "No active version in root.")
                 sys.exit(1)

        self.setup_ui()
        self.refresh()

    def setup_ui(self):
        # Top Bar: Usage
        top_frame = tk.Frame(self.root, bg="#333", height=60)
        top_frame.pack(fill=tk.X, padx=5, pady=5)
        
        lbl_usage = tk.Label(top_frame, text="Disk Usage:", bg="#333", fg="white", font=("Segoe UI", 10, "bold"))
        lbl_usage.pack(side=tk.LEFT, padx=5)
        
        self.progress_var = tk.DoubleVar()
        self.progress_bar = ttk.Progressbar(top_frame, variable=self.progress_var, maximum=100, length=400)
        self.progress_bar.pack(side=tk.LEFT, padx=10, fill=tk.X, expand=True)
        
        self.lbl_stats = tk.Label(top_frame, text="0 / 0 MB", bg="#333", fg="#aaa")
        self.lbl_stats.pack(side=tk.LEFT, padx=10)

        # Nav Bar
        nav_frame = tk.Frame(self.root, bg="#2b2b2b")
        nav_frame.pack(fill=tk.X, padx=5, pady=5)
        
        btn_up = tk.Button(nav_frame, text="⬆ Up", command=self.go_up, bg="#444", fg="white", relief=tk.FLAT)
        btn_up.pack(side=tk.LEFT, padx=5)
        
        self.lbl_path = tk.Label(nav_frame, text="/", bg="#2b2b2b", fg="#4cc2ff", font=("Consolas", 11))
        self.lbl_path.pack(side=tk.LEFT, padx=10)
        
        # Tag for version
        self.lbl_ver = tk.Label(nav_frame, text="[master]", bg="#007acc", fg="white", font=("Consolas", 9, "bold"))
        self.lbl_ver.pack(side=tk.LEFT, padx=5)

        # Treeview
        columns = ("size", "type")
        self.tree = ttk.Treeview(self.root, columns=columns, show='tree headings')
        self.tree.heading("size", text="Size")
        self.tree.heading("type", text="Type")
        self.tree.column("size", width=100, anchor=tk.E)
        self.tree.column("type", width=80, anchor=tk.CENTER)
        
        self.tree.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        self.tree.bind("<Double-1>", self.on_double_click)
        
        # Scrollbar
        scrollbar = ttk.Scrollbar(self.tree, orient=tk.VERTICAL, command=self.tree.yview)
        self.tree.configure(yscroll=scrollbar.set)
        scrollbar.pack(side=tk.RIGHT, fill=tk.Y)

    def refresh(self):
        # Update Usage
        used, total = self.fs.get_usage()
        percent = (used / total) * 100 if total > 0 else 0
        self.progress_var.set(percent)
        
        used_mb = used / (1024*1024)
        total_mb = total / (1024*1024)
        self.lbl_stats.config(text=f"{used_mb:.2f} MB / {total_mb:.2f} MB ({percent:.1f}%)")
        
        # Update Bar Colors (simple logic)
        # Tkinter progress bar color styling is tricky without themes, allowing default blue.

        # Update Path
        self.lbl_path.config(text=self.current_path)
        self.lbl_ver.config(text=f"[{self.current_version}]")

        # Clear List
        for item in self.tree.get_children():
            self.tree.delete(item)

        # Populate
        entries = self.fs.list_dir(self.current_content_cluster)
        
        # Sort folders first
        entries.sort(key=lambda x: (x.type, x.name))
        # Wait, type 2 (DIR) > type 1 (FILE). We want Dirs first.
        entries.sort(key=lambda x: (0 if x.type == fs_driver.TYPE_LEVELED_DIR else 1, x.name))

        for e in entries:
            name = e.name
            size_str = f"{e.size} B"
            type_str = "FILE"
            icon = "📄 "
            
            if e.type == fs_driver.TYPE_LEVELED_DIR:
                type_str = "DIR"
                size_str = "-"
                icon = "📁 "
            
            self.tree.insert("", tk.END, iid=e.name, text=f"{icon}{name}", values=(size_str, type_str))
            
        self.entries_map = {e.name: e for e in entries}

    def on_double_click(self, event):
        item_id = self.tree.selection()[0]
        if not item_id: return
        
        entry = self.entries_map.get(item_id)
        if not entry: return
        
        if entry.type == fs_driver.TYPE_LEVELED_DIR:
            self.enter_folder(entry)
        elif entry.type == fs_driver.TYPE_FILE:
            self.view_file(entry)

    def enter_folder(self, entry):
        # List levels
        levels = self.fs.list_levels(entry.start_cluster)
        if not levels:
            messagebox.showinfo("Info", "This folder has no active levels.")
            return

        target_ver = ""
        if len(levels) == 1:
            target_ver = levels[0].name
        else:
            # Popup
            names = [l.name for l in levels]
            res = LevelSelectionDialog(self.root, names, entry.name).result
            if not res: return
            target_ver = res

        # Find content cluster
        content_cluster = 0
        for l in levels:
            if l.name == target_ver:
                content_cluster = l.content_cluster
                break
        
        if content_cluster:
            # Push History
            state = (self.current_path, self.current_dir_cluster, self.current_content_cluster, self.current_version)
            self.history.append(state)
            
            self.current_dir_cluster = entry.start_cluster
            self.current_content_cluster = content_cluster
            self.current_version = target_ver
            
            separator = "" if self.current_path == "/" else "/"
            self.current_path += separator + entry.name
            
            self.refresh()

    def go_up(self):
        if not self.history:
            return
            
        path, d_clus, c_clus, ver = self.history.pop()
        self.current_path = path
        self.current_dir_cluster = d_clus
        self.current_content_cluster = c_clus
        self.current_version = ver
        self.refresh()

    def view_file(self, entry):
        data = self.fs.read_file(entry.start_cluster, entry.size)
        try:
            text = data.decode('utf-8')
        except:
            text = f"<Binary Data: {len(data)} bytes>"
            
        top = tk.Toplevel(self.root)
        top.title(f"Viewing: {entry.name}")
        top.geometry("600x400")
        top.configure(bg="#2b2b2b")
        
        txt = tk.Text(top, bg="#1e1e1e", fg="#d4d4d4", font=("Consolas", 10))
        txt.pack(fill=tk.BOTH, expand=True)
        txt.insert(tk.END, text)
        txt.config(state=tk.DISABLED)

class LevelSelectionDialog(simpledialog.Dialog):
    def __init__(self, parent, levels, folder):
        self.levels = levels
        self.folder = folder
        self.selected = None
        super().__init__(parent, title="Select Level")

    def body(self, master):
        tk.Label(master, text=f"Select level for '{self.folder}':").pack(pady=5)
        self.lb = tk.Listbox(master, height=5)
        for l in self.levels:
            self.lb.insert(tk.END, l)
        self.lb.select_set(0)
        self.lb.pack(padx=10, pady=5)
        return self.lb

    def apply(self):
        sel = self.lb.curselection()
        if sel:
            self.result = self.levels[sel[0]]

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: level_gui <DriveLetter> (e.g. D)")
        sys.exit(1)
        
    root = tk.Tk()
    app = LevelExplorerApp(root, sys.argv[1])
    root.mainloop()
